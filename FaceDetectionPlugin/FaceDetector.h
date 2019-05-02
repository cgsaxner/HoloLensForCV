#pragma once

namespace FaceDetectorPlugin
{
	struct DetectedFace
	{
		Windows::Foundation::Numerics::float4x4 cameraToWorldUnity;

		// Image position
		int x;
		int y;
		int width;
		int height;
	};

	class FaceDetector
	{
	public:
		FaceDetector();
		~FaceDetector();

		void Initialize(int* iSpatialCoordinateSystem);
		void Dispose();
		bool DetectFace();
		bool GetDetectedFacePose(float* position, float* rotation, float* cameraToWorldUnity);

	private:
		// all sensor types that will be enabled
		std::vector<HoloLensForCV::SensorType> _enabledSensorTypes;

		// HoloLens media frame source group for reading the sensor streams
		HoloLensForCV::MediaFrameSourceGroup^ _rgbFrameSourceGroup;
		std::atomic_bool _rgbFrameSourceGroupStarted;

		// Note that we have to create a separate media frame source group for the research mode sensors.
		HoloLensForCV::MediaFrameSourceGroup^ _depthFrameSourceGroup;
		std::atomic_bool _depthFrameSourceGroupStarted;

		// Cascade classifier for face detection
		cv::CascadeClassifier _faceCascade;

		Windows::Foundation::DateTime _photoVideoPreviewTimestamp;
		Windows::Foundation::DateTime _depthPreviewTimestamp;

		// Camera intrinsics
		cv::Mat _unprojectionMap;
		cv::Mat _photoVideoIntrinsics = cv::Mat::eye(4, 4, CV_32F);

		// Unity coordinate system
		Windows::Perception::Spatial::SpatialCoordinateSystem^ _unitySpatialCoordinateSystem;

		// Spatial Perception
		HoloLensForCV::SpatialPerception^ _spatialPerception;

		// Multiframe Buffers
		HoloLensForCV::MultiFrameBuffer^ _rgbFrameBuffer;
		HoloLensForCV::MultiFrameBuffer^ _depthFrameBuffer;

		DetectedFace _detectedFace;

	private:
		// Initializes access to HoloLens sensors.
		void StartHoloLensMediaFrameSourceGroup();

		DetectedFace DetectFaceCascade(Windows::Perception::Spatial::SpatialCoordinateSystem^ unitySpatialCoordinateSystem, HoloLensForCV::SensorFrame^ frame);

		
		
	};
}


