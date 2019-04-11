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

		if (_photoVideoPreviewTimestamp.UniversalTime == latestPhotoVideoPreviewFrame->Timestamp.UniversalTime || _depthPreviewTimestamp.UniversalTime == latestDepthPreviewFrame->Timestamp.UniversalTime)
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

		//if (_photoVideoPreviewTexture == nullptr)
		//{
		//	_photoVideoPreviewTexture = std::make_shared<Rendering::Texture2D>(_deviceResources,
		//		latestPhotoVideoPreviewFrame->SoftwareBitmap->PixelWidth,
		//		latestPhotoVideoPreviewFrame->SoftwareBitmap->PixelHeight,
		//		photoVideoPreviewTextureFormat);
		//}

		//if (_depthPreviewTexture == nullptr)
		//{
		//	_depthPreviewTexture = std::make_shared<Rendering::Texture2D>(_deviceResources,
		//		latestDepthPreviewFrame->SoftwareBitmap->PixelWidth,
		//		latestDepthPreviewFrame->SoftwareBitmap->PixelHeight,
		//		depthPreviewTextureFormat);
		//}

		//{
		//	void* mappedTexture = _photoVideoPreviewTexture->MapCPUTexture<void>(D3D11_MAP_WRITE /* mapType */);
		//	Windows::Graphics::Imaging::SoftwareBitmap^ bitmap = latestPhotoVideoPreviewFrame->SoftwareBitmap;

		//	REQUIRES(photoVideoPreviewExpectedBitmapPixelFormat == bitmap->BitmapPixelFormat);
		//	Windows::Graphics::Imaging::BitmapBuffer^ bitmapBuffer = bitmap->LockBuffer(Windows::Graphics::Imaging::BitmapBufferAccessMode::Read);

		//	uint32_t pixelBufferDataLength = 0;

		//	uint8_t* pixelBufferData = Io::GetTypedPointerToMemoryBuffer<uint8_t>(bitmapBuffer->CreateReference(), pixelBufferDataLength);

		//	const int32_t bytesToCopy = _photoVideoPreviewTexture->GetWidth() *  _photoVideoPreviewTexture->GetHeight() * photoVideoPreviewPixelStride;

		//	ASSERT(static_cast<uint32_t>(bytesToCopy) == pixelBufferDataLength);
		//	ASSERT(0 == memcpy_s(mappedTexture, bytesToCopy, pixelBufferData, pixelBufferDataLength));

		//	_photoVideoPreviewTexture->UnmapCPUTexture();
		//}

		//{
		//	void* mappedTexture = _depthPreviewTexture->MapCPUTexture<void>(D3D11_MAP_WRITE /* mapType */);
		//	Windows::Graphics::Imaging::SoftwareBitmap^ bitmap = latestDepthPreviewFrame->SoftwareBitmap;

		//	REQUIRES(depthPreviewExpectedBitmapPixelFormat == bitmap->BitmapPixelFormat);
		//	Windows::Graphics::Imaging::BitmapBuffer^ bitmapBuffer = bitmap->LockBuffer(Windows::Graphics::Imaging::BitmapBufferAccessMode::Read);

		//	uint32_t pixelBufferDataLength = 0;

		//	uint8_t* pixelBufferData = Io::GetTypedPointerToMemoryBuffer<uint8_t>(bitmapBuffer->CreateReference(), pixelBufferDataLength);

		//	const int32_t bytesToCopy = _depthPreviewTexture->GetWidth() * _depthPreviewTexture->GetHeight() * depthPreviewPixelStride;

		//	ASSERT(static_cast<uint32_t>(bytesToCopy) == pixelBufferDataLength);
		//	ASSERT(0 == memcpy_s(mappedTexture, bytesToCopy, pixelBufferData, pixelBufferDataLength));

		//	_depthPreviewTexture->UnmapCPUTexture();
		//}

		//_photoVideoPreviewTexture->CopyCPU2GPU();
		//_depthPreviewTexture->CopyCPU2GPU();
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

	cv::Mat AppMain::LoadUnprojectionMap(std::string file_path, int w, int h)
	{
		cv::Mat unprojection_map(cv::Size(w, h), CV_32FC2);

		std::ifstream ifs(file_path, std::ios::binary | std::ios::ate);
		if (!ifs.is_open()) {
			std::cout << "Could not open or find depth map binary file" << std::endl;
			ifs.close();
		}

		// read all bytes
		std::ifstream::pos_type pos = ifs.tellg();
		std::vector<char> result(pos);

		ifs.seekg(0, std::ios::beg);
		ifs.read(&result[0], pos);

		std::vector<float> u_vals, v_vals;
		for (int i = 0; i < result.size(); i += 8)
		{
			float val_u, val_v;
			uchar byte_array_u[] = { result[i], result[i + 1], result[i + 2], result[i + 3] };
			uchar byte_array_v[] = { result[i + 4], result[i + 5], result[i + 6], result[i + 7] };
			std::copy(reinterpret_cast<const char*>(&byte_array_u[0]), reinterpret_cast<const char*>(&byte_array_u[4]), reinterpret_cast<char*>(&val_u));
			std::copy(reinterpret_cast<const char*>(&byte_array_v[0]), reinterpret_cast<const char*>(&byte_array_v[4]), reinterpret_cast<char*>(&val_v));
			u_vals.push_back(val_u);
			v_vals.push_back(val_v);
		}
		for (int c = 0; c < unprojection_map.cols; c++)
		{
			for (int r = 0; r < unprojection_map.rows; r++)
			{
				unprojection_map.at<cv::Vec2f>(r, c)[0] = u_vals[c * unprojection_map.rows + r];
				unprojection_map.at<cv::Vec2f>(r, c)[1] = v_vals[c * unprojection_map.rows + r];
			}
		}

		return unprojection_map;
	}
}