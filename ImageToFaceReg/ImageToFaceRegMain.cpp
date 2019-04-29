#include "pch.h"
#include "ImageToFaceRegMain.h"

using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Storage;
using namespace Windows::Foundation::Collections;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;
using namespace Platform::Collections;
using namespace concurrency;

namespace ImageToFaceReg
{
	// Loads and initializes application assets when the application is loaded.
	AppMain::AppMain(const std::shared_ptr<Graphics::DeviceResources>& deviceResources)
		: Holographic::AppMainBase(deviceResources)
		, _photoVideoMediaFrameSourceGroupStarted(false)
		, _researchModeMediaFrameSourceGroupStarted(false)
	{
		_enabledSensorTypes = {
			HoloLensForCV::SensorType::PhotoVideo,
			HoloLensForCV::SensorType::ShortThrowToFDepth
		};

		// initialize face detector
		StorageFolder^ folder = Windows::ApplicationModel::Package::Current->InstalledLocation;
		auto pathString = folder->Path->Begin();
		std::wstring s(pathString);
		s += std::wstring(L"\\Assets\\lbpcascade_frontalface_improved.xml");
		std::string pathToCascade(s.begin(), s.end());

		if (!_faceCascade.load(pathToCascade))
		{
			dbg::trace(L"Error loading cascade definition.");
		}
	}

	void AppMain::OnHolographicSpaceChanged(
		Windows::Graphics::Holographic::HolographicSpace^ holographicSpace)
	{
		// Initialize the camera preview hologram.
		_slateRenderer = std::make_unique<Rendering::SlateRenderer>(_deviceResources);

		// Initialize the HoloLens media frame readers
		StartHoloLensMediaFrameSourceGroup();
	}

	void AppMain::OnSpatialInput(_In_ Windows::UI::Input::Spatial::SpatialInteractionSourceState^ pointerState)
	{
		Windows::Perception::Spatial::SpatialCoordinateSystem^ currentCoordinateSystem =
			_spatialPerception->GetOriginFrameOfReference()->CoordinateSystem;

		// When a Pressed gesture is detected, the sample hologram will be repositioned
		// two meters in front of the user.
		_slateRenderer->PositionHologram(pointerState->TryGetPointerPose(currentCoordinateSystem));
	}

	// Updates the application state once per frame.
	void AppMain::OnUpdate(
		_In_ Windows::Graphics::Holographic::HolographicFrame^ holographicFrame,
		_In_ const Graphics::StepTimer& stepTimer)
	{
		dbg::TimerGuard timerGuard(
			L"AppMain::OnUpdate",
			30.0 /* minimum_time_elapsed_in_milliseconds */);

		// Update scene objects.
		// Put time-based updates here. By default this code will run once per frame,
		// but if you change the StepTimer to use a fixed time step this code will
		// run as many times as needed to get to the current step.

		_slateRenderer->Update(stepTimer);

		if (!_photoVideoMediaFrameSourceGroupStarted || !_researchModeMediaFrameSourceGroupStarted)
		{
			return;
		}

		HoloLensForCV::SensorFrame^ latestPhotoVideoPreviewFrame;
		HoloLensForCV::SensorFrame^ latestDepthPreviewFrame;

		Windows::Graphics::Imaging::BitmapPixelFormat photoVideoPreviewExpectedBitmapPixelFormat;
		Windows::Graphics::Imaging::BitmapPixelFormat depthPreviewExpectedBitmapPixelFormat;
		DXGI_FORMAT photoVideoPreviewTextureFormat;
		DXGI_FORMAT depthPreviewTextureFormat;
		int32_t photoVideoPreviewPixelStride;
		int32_t depthPreviewPixelStride;

		photoVideoPreviewExpectedBitmapPixelFormat = Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8;
		photoVideoPreviewTextureFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
		photoVideoPreviewPixelStride = 4;

		depthPreviewExpectedBitmapPixelFormat = Windows::Graphics::Imaging::BitmapPixelFormat::Gray8;
		depthPreviewTextureFormat = DXGI_FORMAT_R8_UNORM;
		depthPreviewPixelStride = 1;


		latestPhotoVideoPreviewFrame = _photoVideoMediaFrameSourceGroup->GetLatestSensorFrame(HoloLensForCV::SensorType::PhotoVideo);
		latestDepthPreviewFrame = _researchModeMediaFrameSourceGroup->GetLatestSensorFrame(HoloLensForCV::SensorType::ShortThrowToFDepth);


		if (latestPhotoVideoPreviewFrame == nullptr || latestDepthPreviewFrame == nullptr)
		{
			return;
		}

		if (_unprojectionMap.empty())
		{
			auto depthIntrinsics = latestDepthPreviewFrame->SensorStreamingCameraIntrinsics;
			auto photoVideoProjectionTransform = latestPhotoVideoPreviewFrame->CameraProjectionTransform;

			Float4x4ToCvMat(photoVideoProjectionTransform, _photoVideoIntrinsics);
			GetUnprojectionMap(depthIntrinsics, _unprojectionMap);
		}

		// dbg::trace(L"Depth intrinsics loaded. Height is %i, Width is %i.", depthIntrinsics->ImageHeight, depthIntrinsics->ImageWidth);
		dbg::trace(L"Photo Video intrinsics loaded. fx is %f, fy is %f.", _photoVideoIntrinsics.at<float>(0, 0), _photoVideoIntrinsics.at<float>(1, 1));

		if (_photoVideoPreviewTimestamp.UniversalTime == latestPhotoVideoPreviewFrame->Timestamp.UniversalTime || 
			_depthPreviewTimestamp.UniversalTime == latestDepthPreviewFrame->Timestamp.UniversalTime)
		{
			return;
		}

		_photoVideoPreviewTimestamp = latestPhotoVideoPreviewFrame->Timestamp;
		_depthPreviewTimestamp = latestDepthPreviewFrame->Timestamp;

		cv::Mat matRgbImg, matDepthImg, grayImg, smallImg;

		std::vector<cv::Rect> faces;

		rmcv::WrapHoloLensSensorFrameWithCvMat(latestPhotoVideoPreviewFrame, matRgbImg);
		rmcv::WrapHoloLensSensorFrameWithCvMat(latestDepthPreviewFrame, matDepthImg);

		cv::cvtColor(matRgbImg, grayImg, cv::COLOR_BGRA2GRAY);
		double scale = 4;
		double fx = 1 / scale;

		// Resize the Grayscale Image  
		cv::resize(grayImg, smallImg, cv::Size(), fx, fx, cv::INTER_LINEAR);

		cv::equalizeHist(smallImg, smallImg);

		_faceCascade.detectMultiScale(smallImg, faces, 1.1, 2, 0 | cv::CASCADE_SCALE_IMAGE, cv::Size(30, 30));

		dbg::trace(L"Found %i faces", faces.size());

		for (size_t i = 0; i < faces.size(); i++)
		{
			
			cv::Rect r = faces[i];

			cv::Rect rScaled(cv::Point(cvRound(r.x*scale), cvRound(r.y  *scale)),
				cv::Point(cvRound((r.x + r.width - 1) * scale), cvRound((r.y + r.height - 1)*scale)));

			cv::rectangle(matRgbImg, rScaled, cv::Scalar(255, 0, 0));
		}

		OpenCVHelpers::CreateOrUpdateTexture2D(_deviceResources, matRgbImg, _photoVideoPreviewTexture);
		OpenCVHelpers::CreateOrUpdateTexture2D(_deviceResources, matDepthImg, _depthPreviewTexture);

	}


	void AppMain::OnPreRender()
	{
	}

	// Renders the current frame to each holographic camera, according to the
	// current application and spatial positioning state.
	void AppMain::OnRender()
	{
		// Draw the sample hologram.
		_slateRenderer->Render(_photoVideoPreviewTexture);
	}

	// Notifies classes that use Direct3D device resources that the device resources
	// need to be released before this method returns.
	void AppMain::OnDeviceLost()
	{
		_slateRenderer->ReleaseDeviceDependentResources();

		_photoVideoMediaFrameSourceGroup = nullptr;
		_researchModeMediaFrameSourceGroup = nullptr;

		_photoVideoMediaFrameSourceGroupStarted = false;
		_researchModeMediaFrameSourceGroupStarted = false;

		_photoVideoPreviewTexture.reset();
		_depthPreviewTexture.reset();
	}

	// Notifies classes that use Direct3D device resources that the device resources
	// may now be recreated.
	void AppMain::OnDeviceRestored()
	{
		_slateRenderer->CreateDeviceDependentResources();
		StartHoloLensMediaFrameSourceGroup();
	}

	// Called when the application is suspending.
	void AppMain::SaveAppState()
	{
		if (_photoVideoMediaFrameSourceGroup == nullptr && _researchModeMediaFrameSourceGroup == nullptr)
			return;

		concurrency::create_task(_photoVideoMediaFrameSourceGroup->StopAsync()).then(
			[&]()
		{
			delete _photoVideoMediaFrameSourceGroup;
			_photoVideoMediaFrameSourceGroup = nullptr;
			_photoVideoMediaFrameSourceGroupStarted = false;
		}).wait();

		concurrency::create_task(_researchModeMediaFrameSourceGroup->StopAsync()).then(
			[&]()
		{
			delete _researchModeMediaFrameSourceGroup;
			_researchModeMediaFrameSourceGroup = nullptr;
			_researchModeMediaFrameSourceGroupStarted = false;
		}).wait();
	}

	// Called when the application is resuming.
	void AppMain::LoadAppState()
	{
		StartHoloLensMediaFrameSourceGroup();
	}

	void AppMain::StartHoloLensMediaFrameSourceGroup()
	{
		REQUIRES(
			!_photoVideoMediaFrameSourceGroupStarted &&
			!_researchModeMediaFrameSourceGroupStarted &&
			nullptr != _spatialPerception);

		_photoVideoMediaFrameSourceGroup =
			ref new HoloLensForCV::MediaFrameSourceGroup(
				HoloLensForCV::MediaFrameSourceGroupType::PhotoVideoCamera,
				_spatialPerception, nullptr);

		_researchModeMediaFrameSourceGroup =
			ref new HoloLensForCV::MediaFrameSourceGroup(
				HoloLensForCV::MediaFrameSourceGroupType::HoloLensResearchModeSensors,
				_spatialPerception, nullptr);

		for (const auto enabledSensorType : _enabledSensorTypes)
		{
			if (enabledSensorType == HoloLensForCV::SensorType::PhotoVideo)
			{
				_photoVideoMediaFrameSourceGroup->Enable(enabledSensorType);
			}
			else
			{
				_researchModeMediaFrameSourceGroup->Enable(enabledSensorType);
			}
		}

		// Start the media frame source groups.
		auto startPhotoVideoMediaFrameSourceGroupTask =
			concurrency::create_task(
				_photoVideoMediaFrameSourceGroup->StartAsync());

		startPhotoVideoMediaFrameSourceGroupTask.then([&]() {
			_photoVideoMediaFrameSourceGroupStarted = true;
		});

		auto startReseachModeMediaFrameSourceGroupTask =
			concurrency::create_task(
				_researchModeMediaFrameSourceGroup->StartAsync());

		startReseachModeMediaFrameSourceGroupTask.then([&]() {
			_researchModeMediaFrameSourceGroupStarted = true;
		});
	}

	void AppMain::GetUnprojectionMap(_In_ HoloLensForCV::CameraIntrinsics^ researchModeSensorIntrinsics, 
		_Inout_ cv::Mat& unprojectionMap)
	{
		int w = researchModeSensorIntrinsics->ImageWidth;
		int h = researchModeSensorIntrinsics->ImageHeight;

		unprojectionMap = cv::Mat::zeros(cv::Size(w, h), CV_32FC2);

		std::vector<float> u_vals, v_vals;

		for (unsigned int x = 0; x < w; ++x)
		{
			for (unsigned int y = 0; y < h; ++y)
			{
				Windows::Foundation::Point uv = { float(x), float(y) }, xy;
				researchModeSensorIntrinsics->MapImagePointToCameraUnitPlane(uv, &xy);
				u_vals.push_back(xy.X);
				v_vals.push_back(xy.Y);
			}
		}

		for (int c = 0; c < unprojectionMap.cols; c++)
		{
			for (int r = 0; r < unprojectionMap.rows; r++)
			{
				unprojectionMap.at<cv::Vec2f>(r, c)[0] = u_vals[c * unprojectionMap.rows + r];
				unprojectionMap.at<cv::Vec2f>(r, c)[1] = v_vals[c * unprojectionMap.rows + r];
			}
		}
	}

	void AppMain::Float4x4ToCvMat(_In_ Windows::Foundation::Numerics::float4x4 floatArray, _Inout_ cv::Mat& cvMat)
	{
		REQUIRES(cvMat.rows == 4 &&
			cvMat.cols == 4 &&
			cvMat.depth() == CV_32F);

		cvMat.at<float>(0, 0) = floatArray.m11;
		cvMat.at<float>(0, 1) = floatArray.m12;
		cvMat.at<float>(0, 2) = floatArray.m13;
		cvMat.at<float>(0, 3) = floatArray.m14;

		cvMat.at<float>(1, 0) = floatArray.m21;
		cvMat.at<float>(1, 1) = floatArray.m22;
		cvMat.at<float>(1, 2) = floatArray.m23;
		cvMat.at<float>(1, 3) = floatArray.m24;

		cvMat.at<float>(2, 0) = floatArray.m31;
		cvMat.at<float>(2, 1) = floatArray.m32;
		cvMat.at<float>(2, 2) = floatArray.m33;
		cvMat.at<float>(2, 3) = floatArray.m34;

		cvMat.at<float>(3, 0) = floatArray.m41;
		cvMat.at<float>(3, 1) = floatArray.m42;
		cvMat.at<float>(3, 2) = floatArray.m43;
		cvMat.at<float>(3, 3) = floatArray.m44;
	}

	std::vector<cv::Point2f> AppMain::MapRgb2Depth(HoloLensForCV::SensorFrame rgbFrame, HoloLensForCV::SensorFrame depthFrame, std::vector<cv::Point2f> rgbPoints)
	{

	}
}