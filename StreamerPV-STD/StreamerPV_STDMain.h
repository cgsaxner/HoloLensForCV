#pragma once

namespace StreamerPV_STD
{
	class AppMain : public Holographic::AppMainBase
	{
	public:
		AppMain(
			const std::shared_ptr<Graphics::DeviceResources>& deviceResources);

		// IDeviceNotify
		virtual void OnDeviceLost() override;

		virtual void OnDeviceRestored() override;

		// IAppMain
		virtual void OnHolographicSpaceChanged(
			_In_ Windows::Graphics::Holographic::HolographicSpace^ holographicSpace) override;

		virtual void OnSpatialInput(
			_In_ Windows::UI::Input::Spatial::SpatialInteractionSourceState^ pointerState) override;

		virtual void OnUpdate(
			_In_ Windows::Graphics::Holographic::HolographicFrame^ holographicFrame,
			_In_ const Graphics::StepTimer& stepTimer) override;

		virtual void OnPreRender() override;

		virtual void OnRender() override;

		virtual void LoadAppState() override;

		virtual void SaveAppState() override;

	private:
		// Initializes access to HoloLens sensors.
		void StartHoloLensMediaFrameSourceGroup();

	private:
		// all sensor types that will be enabled
		std::vector<HoloLensForCV::SensorType> _enabledSensorTypes;

		// HoloLens media frame source group for reading the sensor streams.
		HoloLensForCV::MediaFrameSourceGroup^ _photoVideoMediaFrameSourceGroup;
		std::atomic_bool _photoVideoMediaFrameSourceGroupStarted;

		// Note that we have to create a separate media frame source group for the research mode sensors.
		HoloLensForCV::MediaFrameSourceGroup^ _researchModeMediaFrameSourceGroup;
		std::atomic_bool _researchModeMediaFrameSourceGroupStarted;

		// HoloLens media frame server manager
		HoloLensForCV::SensorFrameStreamer^ _sensorFrameStreamer;
		std::atomic_bool _sensorFrameStreamerStarted;

		// Camera preview
		std::unique_ptr<Rendering::SlateRenderer> _slateRenderer;
		Rendering::Texture2DPtr _cameraPreviewTexture;
		Windows::Foundation::DateTime _cameraPreviewTimestamp;
	};
}
