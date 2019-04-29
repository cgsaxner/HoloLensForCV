#pragma once


// Updates, renders, and presents holographic content using Direct3D.
namespace ImageToFaceReg
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

		// Converts Windows float4x4 array to cv Mat
		void Float4x4ToCvMat(_In_ Windows::Foundation::Numerics::float4x4 floatArray, _Inout_ cv::Mat& cvMat);

		// Gets unprojeciton map from research mode sensors
		void GetUnprojectionMap(_In_ HoloLensForCV::CameraIntrinsics^ researchModeSensorIntrinsics, _Inout_ cv::Mat& unprojectionMap);

		std::vector<cv::Point2f> MapRgb2Depth(HoloLensForCV::SensorFrame rgbFrame, HoloLensForCV::SensorFrame depthFrame, std::vector<cv::Point2f> rgbPoints);

	private:

		// all sensor types that will be enabled
		std::vector<HoloLensForCV::SensorType> _enabledSensorTypes;

	    // HoloLens media frame source group for reading the sensor streams.
		HoloLensForCV::MediaFrameSourceGroup^ _photoVideoMediaFrameSourceGroup;
		std::atomic_bool _photoVideoMediaFrameSourceGroupStarted;

		// Note that we have to create a separate media frame source group for the research mode sensors.
		HoloLensForCV::MediaFrameSourceGroup^ _researchModeMediaFrameSourceGroup;
		std::atomic_bool _researchModeMediaFrameSourceGroupStarted;

		// Cascade classifier for face detection
		cv::CascadeClassifier _faceCascade;

		// Camera preview
		std::unique_ptr<Rendering::SlateRenderer> _slateRenderer;

		Rendering::Texture2DPtr _photoVideoPreviewTexture;
		Rendering::Texture2DPtr _depthPreviewTexture;

		Windows::Foundation::DateTime _photoVideoPreviewTimestamp;
		Windows::Foundation::DateTime _depthPreviewTimestamp;

		// Camera intrinsics
		cv::Mat _unprojectionMap;
		cv::Mat _photoVideoIntrinsics = cv::Mat::eye(4, 4, CV_32F);
	};
}
