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

			dbg::trace(L"Start unprojection map loading...");
			LoadUnprojectionMap(depthIntrinsics->ImageWidth, depthIntrinsics->ImageHeight);
			// GetUnprojectionMap(depthIntrinsics, _unprojectionMap);
		}

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

		if (faces.size() > 0)
		{
			dbg::trace(L"Found %i faces", faces.size());
			cv::Rect faceSmall = faces[0];

			cv::Rect faceRect(cv::Point(cvRound(faceSmall.x * scale), cvRound(faceSmall.y  * scale)),
				cv::Point(cvRound((faceSmall.x + faceSmall.width - 1) * scale), cvRound((faceSmall.y + faceSmall.height - 1) * scale)));

			std::vector<cv::Point2f> rgbBBox, depthBBox;
			rgbBBox.push_back(cv::Point2f(faceRect.x, faceRect.y));
			rgbBBox.push_back(cv::Point2f(faceRect.x + faceRect.width, faceRect.y));
			rgbBBox.push_back(cv::Point2f(faceRect.x + faceRect.width, faceRect.y + faceRect.height));
			rgbBBox.push_back(cv::Point2f(faceRect.x, faceRect.y + faceRect.height));

			depthBBox = MapRgb2Depth(latestPhotoVideoPreviewFrame, latestDepthPreviewFrame, rgbBBox);
		}
		
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

	IBuffer^ getBufferFromString(Platform::String^ str)
	{
		InMemoryRandomAccessStream^ memoryStream = ref new InMemoryRandomAccessStream();
		DataWriter^ dataWriter = ref new DataWriter(memoryStream);
		dataWriter->WriteString(str);
		return dataWriter->DetachBuffer();
	}

	void AppMain::LoadUnprojectionMap(int w, int h)
	{
		cv::Mat unprojectionMap = cv::Mat(cv::Size(w, h), CV_32FC2);

		create_task(KnownFolders::GetFolderForUserAsync(nullptr, KnownFolderId::PicturesLibrary)).then([this](StorageFolder^ picturesFolder)
		{
			return picturesFolder->GetFileAsync("short_throw_depth_camera_space_projection.bin");
		}).then([this](task<StorageFile^> task)
		{
			try
			{
				StorageFile^ file = task.get();
				auto name = file->Name;
				return file->OpenAsync(FileAccessMode::Read);
			}
			catch (Platform::Exception^ e)
			{
				dbg::trace(L"Error opening file.");
			}
		}).then([this, unprojectionMap](task<IRandomAccessStream^> task)
		{
			uint64 length = 0;
			BYTE *extracted;
			std::vector<unsigned char> result;
			IRandomAccessStream^  inputStream = task.get();
			length = inputStream->Size;
			IBuffer^ buffer = ref new Buffer(inputStream->Size);
			inputStream->ReadAsync(buffer, inputStream->Size, InputStreamOptions::None);
			DataReader^ reader = DataReader::FromBuffer(buffer);

			std::vector<unsigned char> data(reader->UnconsumedBufferLength);
			if (!data.empty())
				reader->ReadBytes(Platform::ArrayReference<unsigned char>(&data[0], data.size()));

			std::vector<float> u_vals, v_vals;
			for (int i = 0; i < data.size(); i += 8)
			{
				float val_u, val_v;
				uchar byte_array_u[] = { result[i], result[i + 1], result[i + 2], result[i + 3] };
				uchar byte_array_v[] = { result[i + 4], result[i + 5], result[i + 6], result[i + 7] };
				std::copy(reinterpret_cast<const char*>(&byte_array_u[0]), reinterpret_cast<const char*>(&byte_array_u[4]), reinterpret_cast<char*>(&val_u));
				std::copy(reinterpret_cast<const char*>(&byte_array_v[0]), reinterpret_cast<const char*>(&byte_array_v[4]), reinterpret_cast<char*>(&val_v));
				u_vals.push_back(val_u);
				v_vals.push_back(val_v);
			}
			for (int c = 0; c < unprojectionMap.cols; c++)
			{
				for (int r = 0; r < unprojectionMap.rows; r++)
				{
					unprojectionMap.at<cv::Vec2f>(r, c)[0] = u_vals[c * unprojectionMap.rows + r];
					unprojectionMap.at<cv::Vec2f>(r, c)[1] = v_vals[c * unprojectionMap.rows + r];
				}
			}

			//extracted = new BYTE[buffer->Length];
			//reader->ReadBytes(Platform::ArrayReference<BYTE>(extracted, buffer->Length));
		});

		
	}


	void AppMain::GetUnprojectionMap(_In_ HoloLensForCV::CameraIntrinsics^ researchModeSensorIntrinsics, 
		_Inout_ cv::Mat& unprojectionMap)
	{
		int w = researchModeSensorIntrinsics->ImageWidth;
		int h = researchModeSensorIntrinsics->ImageHeight;

		dbg::trace(L"Load unprojection map...");
		dbg::trace(L"%i", w);
		dbg::trace(L"%i", h);

		unprojectionMap = cv::Mat::zeros(cv::Size(w, h), CV_32FC2);

		std::vector<float> u_vals, v_vals;

		for (unsigned int x = 0; x < w; ++x)
		{
			for (unsigned int y = 0; y < h; ++y)
			{
				Windows::Foundation::Point uv = {float(x), float(y)}, xy;
				dbg::trace(L"%f", uv.X);
				dbg::trace(L"%f", uv.Y);
				researchModeSensorIntrinsics->MapImagePointToCameraUnitPlane(uv, &xy);
				u_vals.push_back(xy.X);
				v_vals.push_back(xy.Y);
				dbg::trace(L"%f", xy.X);
				dbg::trace(L"%f", xy.Y);
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

	void DebugOutputMatrix(std::wstring prompt, float4x4 transform)
	{
		prompt += std::to_wstring(transform.m11) + L", ";
		prompt += std::to_wstring(transform.m12) + L", ";
		prompt += std::to_wstring(transform.m13) + L", ";
		prompt += std::to_wstring(transform.m14) + L"\n";
		prompt += std::to_wstring(transform.m21) + L", ";
		prompt += std::to_wstring(transform.m22) + L", ";
		prompt += std::to_wstring(transform.m23) + L", ";
		prompt += std::to_wstring(transform.m24) + L"\n";
		prompt += std::to_wstring(transform.m31) + L", ";
		prompt += std::to_wstring(transform.m32) + L", ";
		prompt += std::to_wstring(transform.m33) + L", ";
		prompt += std::to_wstring(transform.m34) + L"\n";
		prompt += std::to_wstring(transform.m41) + L", ";
		prompt += std::to_wstring(transform.m42) + L", ";
		prompt += std::to_wstring(transform.m43) + L", ";
		prompt += std::to_wstring(transform.m44);
		dbg::trace(prompt.data());
	}

	void DebugOutputMatrix(std::wstring prompt, cv::Mat transform)
	{
		prompt += std::to_wstring(transform.at<float>(0, 0)) + L", ";
		prompt += std::to_wstring(transform.at<float>(1, 0)) + L", ";
		prompt += std::to_wstring(transform.at<float>(2, 0)) + L", ";
		prompt += std::to_wstring(transform.at<float>(3, 0)) + L"\n";
		prompt += std::to_wstring(transform.at<float>(0, 1)) + L", ";
		prompt += std::to_wstring(transform.at<float>(1, 1)) + L", ";
		prompt += std::to_wstring(transform.at<float>(2, 1)) + L", ";
		prompt += std::to_wstring(transform.at<float>(3, 1)) + L"\n";
		prompt += std::to_wstring(transform.at<float>(0, 2)) + L", ";
		prompt += std::to_wstring(transform.at<float>(1, 2)) + L", ";
		prompt += std::to_wstring(transform.at<float>(2, 2)) + L", ";
		prompt += std::to_wstring(transform.at<float>(3, 2)) + L"\n";
		prompt += std::to_wstring(transform.at<float>(0, 3)) + L", ";
		prompt += std::to_wstring(transform.at<float>(1, 3)) + L", ";
		prompt += std::to_wstring(transform.at<float>(2, 3)) + L", ";
		prompt += std::to_wstring(transform.at<float>(3, 3));
		dbg::trace(prompt.data());
	}

	std::vector<cv::Point2f> AppMain::MapRgb2Depth(HoloLensForCV::SensorFrame^ rgbFrame, HoloLensForCV::SensorFrame^ depthFrame, std::vector<cv::Point2f> rgbPoints)
	{
		cv::Mat rgbViewTransform = cv::Mat::eye(4, 4, CV_32F);
		cv::Mat rgbFrameToOrigin = cv::Mat::eye(4, 4, CV_32F);
		cv::Mat depthViewTransform = cv::Mat::eye(4, 4, CV_32F);
		cv::Mat depthFrameToOrigin = cv::Mat::eye(4, 4, CV_32F);

		std::vector<cv::Point2f> depthPoints;

		std::wstring output;

		DebugOutputMatrix(output, rgbFrame->CameraViewTransform);
		DebugOutputMatrix(output, rgbFrame->FrameToOrigin);
		DebugOutputMatrix(output, depthFrame->CameraViewTransform);
		DebugOutputMatrix(output, depthFrame->FrameToOrigin);

		Float4x4ToCvMat(rgbFrame->CameraViewTransform, rgbViewTransform);
		Float4x4ToCvMat(rgbFrame->FrameToOrigin, rgbFrameToOrigin);
		Float4x4ToCvMat(depthFrame->CameraViewTransform, depthViewTransform);
		Float4x4ToCvMat(depthFrame->FrameToOrigin, depthFrameToOrigin);

		DebugOutputMatrix(output, rgbViewTransform);
		DebugOutputMatrix(output, rgbFrameToOrigin);
		DebugOutputMatrix(output, depthViewTransform);
		DebugOutputMatrix(output, depthFrameToOrigin);

		cv::Mat rgbExtrinsics = rgbViewTransform * rgbFrameToOrigin.inv();
		cv::Mat depthExtrinsics = depthViewTransform * depthFrameToOrigin.inv();
		rgbExtrinsics.convertTo(rgbExtrinsics, CV_64F);
		depthExtrinsics.convertTo(depthExtrinsics, CV_64F);

		dbg::trace(L"Matrices created");

		float A = _photoVideoIntrinsics.at<float>(2, 2);
		float fx = _photoVideoIntrinsics.at<float>(0, 0);
		float fy = _photoVideoIntrinsics.at<float>(1, 1);
		float cx = _photoVideoIntrinsics.at<float>(2, 0);
		float cy = _photoVideoIntrinsics.at<float>(2, 1);

		dbg::trace(L"Found intrinsic parameters");

		for (int j = 0; j < rgbPoints.size(); j++)
		{
			cv::Point2d rgbScaled(
				(2.0 * rgbPoints[j].x / rgbFrame->CoreCameraIntrinsics->ImageWidth - 1.0),
				2.0 * (1.0 - rgbPoints[j].y / rgbFrame->CoreCameraIntrinsics->ImageHeight) - 1.0);

			dbg::trace(L"Point scaled: %f, %f", rgbScaled.x, rgbScaled.y);

			cv::Point3d rgbCam(
				(rgbScaled.x - cx / A) / fx,
				(rgbScaled.y - cy / A) / fy,
				1.0 / A);

			dbg::trace(L"Point rgb view: %f, %f, %f", rgbCam.x, rgbCam.y, rgbCam.z);

			cv::Mat rgbCamMat = (cv::Mat_<double>(4, 1) << rgbCam.x * 0.3, rgbCam.y * 0.3, rgbCam.z * 0.3, 1);

			dbg::trace(L"RGB view matrix: %f, %f, %f, %f", rgbCamMat.at<double>(0, 0), rgbCamMat.at<double>(1, 0), rgbCamMat.at<double>(2, 0), rgbCamMat.at<double>(3, 0));

			dbg::trace(L"RGB extrinsics: %f, %f, %f, %f", rgbExtrinsics.at<double>(0, 0), rgbExtrinsics.at<double>(1, 0), rgbExtrinsics.at<double>(2, 0), rgbExtrinsics.at<double>(3, 0));

			cv::Mat worldMat = rgbExtrinsics.inv() * rgbCamMat;
			cv::Mat depthCamMat = depthExtrinsics * worldMat;

			dbg::trace(L"Depth cam: %f, %f, %f", depthCamMat.at<double>(0, 0), depthCamMat.at<double>(1, 0), depthCamMat.at<double>(2, 0));

			cv::Point2f depthScaled(
				depthCamMat.at<double>(0, 0) / depthCamMat.at<double>(2, 0),
				depthCamMat.at<double>(1, 0) / depthCamMat.at<double>(2, 0)
			);

			dbg::trace(L"Pointdepth scaled: %f, %f", rgbScaled.x, rgbScaled.y);

			cv::Mat diff(cv::Size(_unprojectionMap.cols, _unprojectionMap.rows), CV_64FC1);
			for (int c = 0; c < diff.cols; c++)
			{
				for (int r = 0; r < diff.rows; r++)
				{
					float u = _unprojectionMap.at<cv::Vec2f>(r, c)[0];
					dbg::trace(L"%f", u);
					float v = _unprojectionMap.at<cv::Vec2f>(r, c)[1];
					dbg::trace(L"%f", v);
					diff.at<double>(r, c) = abs(u - depthScaled.x) + abs(v - depthScaled.y);
				}
			}

			double min, max;
			cv::Point min_loc, max_loc;
			cv::minMaxLoc(diff, &min, &max, &min_loc, &max_loc);
			depthPoints.push_back(min_loc);
			dbg::trace(L"Point depth frame: %f, %f", min_loc.x, min_loc.y);
		}

		return depthPoints;
	}
}