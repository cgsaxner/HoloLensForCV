//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "pch.h"
#include "AppMain.h"

using namespace Windows::Foundation;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Networking;
using namespace Windows::Networking::Connectivity;
using namespace Windows::Networking::Sockets;
using namespace Windows::Storage::Streams;
using namespace Windows::Graphics::Holographic;
using namespace Windows::Perception::Spatial;
using namespace Windows::UI::Input::Spatial;
using namespace std::placeholders;

namespace StreamerVLC
{
    // Loads and initializes application assets when the application is loaded.
    AppMain::AppMain(const std::shared_ptr<Graphics::DeviceResources>& deviceResources)
        : Holographic::AppMainBase(deviceResources)
		, _photoVideoMediaFrameSourceGroupStarted(false)
		, _researchModeMediaFrameSourceGroupStarted(false)
    {
    }

    void AppMain::OnHolographicSpaceChanged(
        Windows::Graphics::Holographic::HolographicSpace^ holographicSpace)
    {
        //
        // Initialize the camera preview hologram.
        //
        _slateRenderer =
            std::make_unique<Rendering::SlateRenderer>(
                _deviceResources);

        //
        // Initialize the HoloLens media frame readers
        //
        StartHoloLensMediaFrameSourceGroup();
    }

    void AppMain::OnSpatialInput(
        _In_ Windows::UI::Input::Spatial::SpatialInteractionSourceState^ pointerState)
    {
        Windows::Perception::Spatial::SpatialCoordinateSystem^ currentCoordinateSystem =
            _spatialPerception->GetOriginFrameOfReference()->CoordinateSystem;

        // When a Pressed gesture is detected, the sample hologram will be repositioned
        // two meters in front of the user.
        _slateRenderer->PositionHologram(
            pointerState->TryGetPointerPose(currentCoordinateSystem));
    }

    // Updates the application state once per frame.
    void AppMain::OnUpdate(
        _In_ Windows::Graphics::Holographic::HolographicFrame^ holographicFrame,
        _In_ const Graphics::StepTimer& stepTimer)
    {
        dbg::TimerGuard timerGuard(
            L"AppMain::OnUpdate",
            30.0 /* minimum_time_elapsed_in_milliseconds */);

		HoloLensForCV::SensorType renderSensorType = HoloLensForCV::SensorType::VisibleLightLeftFront;

        //
        // Update scene objects.
        //
        // Put time-based updates here. By default this code will run once per frame,
        // but if you change the StepTimer to use a fixed time step this code will
        // run as many times as needed to get to the current step.
        //
        _slateRenderer->Update(
            stepTimer);

        if (!_researchModeMediaFrameSourceGroupStarted || !_photoVideoMediaFrameSourceGroupStarted)
        {
            return;
        }

        HoloLensForCV::SensorFrame^ latestCameraPreviewFrame;
        Windows::Graphics::Imaging::BitmapPixelFormat cameraPreviewExpectedBitmapPixelFormat;
        DXGI_FORMAT cameraPreviewTextureFormat;
        int32_t cameraPreviewPixelStride;

		{
			latestCameraPreviewFrame =
				_researchModeMediaFrameSourceGroup->GetLatestSensorFrame(
					renderSensorType);

			if ((HoloLensForCV::SensorType::ShortThrowToFDepth == renderSensorType) ||
				(HoloLensForCV::SensorType::ShortThrowToFReflectivity == renderSensorType) ||
				(HoloLensForCV::SensorType::LongThrowToFDepth == renderSensorType) ||
				(HoloLensForCV::SensorType::LongThrowToFReflectivity == renderSensorType))
			{
				cameraPreviewExpectedBitmapPixelFormat =
					Windows::Graphics::Imaging::BitmapPixelFormat::Gray8;

				cameraPreviewTextureFormat =
					DXGI_FORMAT_R8_UNORM;

				cameraPreviewPixelStride =
					1;
			}
			else
			{
				cameraPreviewExpectedBitmapPixelFormat =
					Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8;

				cameraPreviewTextureFormat =
					DXGI_FORMAT_B8G8R8A8_UNORM;

				cameraPreviewPixelStride =
					4;
			}
		}

        if (nullptr == latestCameraPreviewFrame)
        {
            return;
        }

        if (_cameraPreviewTimestamp.UniversalTime == latestCameraPreviewFrame->Timestamp.UniversalTime)
        {
            return;
        }

        _cameraPreviewTimestamp = latestCameraPreviewFrame->Timestamp;

        if (nullptr == _cameraPreviewTexture)
        {
#if 0
            dbg::trace(
                L"latestCameraPreviewFrame->SoftwareBitmap->PixelWidth=0x%08x, latestCameraPreviewFrame->SoftwareBitmap->PixelHeight=0x%08x",
                latestCameraPreviewFrame->SoftwareBitmap->PixelWidth, latestCameraPreviewFrame->SoftwareBitmap->PixelHeight);
#endif

            _cameraPreviewTexture =
                std::make_shared<Rendering::Texture2D>(
                    _deviceResources,
                    latestCameraPreviewFrame->SoftwareBitmap->PixelWidth,
                    latestCameraPreviewFrame->SoftwareBitmap->PixelHeight,
                    cameraPreviewTextureFormat);
        }

        {
            void* mappedTexture =
                _cameraPreviewTexture->MapCPUTexture<void>(
                    D3D11_MAP_WRITE /* mapType */);

            Windows::Graphics::Imaging::SoftwareBitmap^ bitmap =
                latestCameraPreviewFrame->SoftwareBitmap;

#if 0
            dbg::trace(
                L"cameraPreviewExpectedBitmapPixelFormat=0x%08x, bitmap->BitmapPixelFormat=0x%08x",
                cameraPreviewExpectedBitmapPixelFormat, bitmap->BitmapPixelFormat);
#endif

            REQUIRES(cameraPreviewExpectedBitmapPixelFormat == bitmap->BitmapPixelFormat);

            Windows::Graphics::Imaging::BitmapBuffer^ bitmapBuffer =
                bitmap->LockBuffer(
                    Windows::Graphics::Imaging::BitmapBufferAccessMode::Read);

            uint32_t pixelBufferDataLength = 0;

            uint8_t* pixelBufferData =
                Io::GetTypedPointerToMemoryBuffer<uint8_t>(
                    bitmapBuffer->CreateReference(),
                    pixelBufferDataLength);

            const int32_t bytesToCopy =
                _cameraPreviewTexture->GetWidth() * _cameraPreviewTexture->GetHeight() * cameraPreviewPixelStride;

            ASSERT(static_cast<uint32_t>(bytesToCopy) == pixelBufferDataLength);

            ASSERT(0 == memcpy_s(
                mappedTexture,
                bytesToCopy,
                pixelBufferData,
                pixelBufferDataLength));

            _cameraPreviewTexture->UnmapCPUTexture();
        }

        _cameraPreviewTexture->CopyCPU2GPU();
    }

    void AppMain::OnPreRender()
    {
    }

    // Renders the current frame to each holographic camera, according to the
    // current application and spatial positioning state.
    void AppMain::OnRender()
    {
        // Draw the sample hologram.
        _slateRenderer->Render(
            _cameraPreviewTexture);
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

		_slateRenderer.reset();
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
        std::vector<HoloLensForCV::SensorType> enabledSensorTypes;

        //
        // Enabling all of the Research Mode sensors at the same time can be quite expensive
        // performance-wise. It's best to scope down the list of enabled sensors to just those
        // that are required for a given task. In this example, we will select the visible
        // light cameras.
        //
        enabledSensorTypes.emplace_back(
            HoloLensForCV::SensorType::VisibleLightLeftLeft);

        enabledSensorTypes.emplace_back(
            HoloLensForCV::SensorType::VisibleLightLeftFront);

        enabledSensorTypes.emplace_back(
            HoloLensForCV::SensorType::VisibleLightRightFront);

        enabledSensorTypes.emplace_back(
            HoloLensForCV::SensorType::VisibleLightRightRight);

		enabledSensorTypes.emplace_back(
			HoloLensForCV::SensorType::PhotoVideo);

        _sensorFrameStreamer =
            ref new HoloLensForCV::SensorFrameStreamer();

        for (const auto enabledSensorType : enabledSensorTypes)
        {
            _sensorFrameStreamer->Enable(
                enabledSensorType);
        }

		_photoVideoMediaFrameSourceGroup =
			ref new HoloLensForCV::MediaFrameSourceGroup(
				HoloLensForCV::MediaFrameSourceGroupType::PhotoVideoCamera,
				_spatialPerception, 
				_sensorFrameStreamer);

        _researchModeMediaFrameSourceGroup =
            ref new HoloLensForCV::MediaFrameSourceGroup(
				HoloLensForCV::MediaFrameSourceGroupType::HoloLensResearchModeSensors,
                _spatialPerception,
                _sensorFrameStreamer);

        for (const auto enabledSensorType : enabledSensorTypes)
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

        concurrency::create_task(_researchModeMediaFrameSourceGroup->StartAsync()).then(
            [&]()
        {
            _researchModeMediaFrameSourceGroupStarted = true;
        });

		concurrency::create_task(_photoVideoMediaFrameSourceGroup->StartAsync()).then(
			[&]()
		{
			_photoVideoMediaFrameSourceGroupStarted = true;
		});
    }
}
