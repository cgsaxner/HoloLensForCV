#include "pch.h"
#include "FaceDetector.h"


using namespace FaceDetectorPlugin;
using namespace Platform;
using namespace Windows::Perception::Spatial;
using namespace Windows::Foundation::Numerics;
using namespace Windows::Storage;

FaceDetector::FaceDetector()
{

}


FaceDetector::~FaceDetector()
{

}

int FaceDetector::Initialize(int* spatialCoordinateSystem)
{
	// Set the sensors to enable
	_enabledSensorTypes = {
		HoloLensForCV::SensorType::PhotoVideo,
		HoloLensForCV::SensorType::ShortThrowToFDepth
	};

	// Initialize face detector
	// Load LBP face cascade. The file has to be placed into the working directory of your Unity application – when you’re within the editor, this is the root project directory.
	dbg::trace(L"Setting up Cascade face detector");

	// initialize face detector
	//StorageFolder^ folder = Windows::ApplicationModel::Package::Current->InstalledLocation;
	//auto pathString = folder->Path->Begin();
	//std::wstring s(pathString);
	//s += std::wstring(L"\\Assets\\lbpcascade_frontalface_improved.xml");
	//std::string pathToCascade(s.begin(), s.end());
	std::string pathToCascade("\\Assets\\lbpcascade_frontalface_improved.xml");

	if (!_faceCascade.load(pathToCascade))
	{
		dbg::trace(L"Failed to load cascade definition.");
		return -1;
	}

	// initialize the coordinate system using the coordinate system provided by unity
	if (spatialCoordinateSystem)
	{
		_unitySpatialCoordinateSystem = reinterpret_cast<SpatialCoordinateSystem^>(spatialCoordinateSystem);
	}
	else
	{
		dbg::trace(L"Creator failed to provide valid ISpatialCoordinateSystem");
		return -2;
	
	}

	dbg::trace(L"Setting up SpatialPerception");
	_spatialPerception = ref new HoloLensForCV::SpatialPerception();

	dbg::trace(L"Creating MultiFrameBuffers");
	_rgbFrameBuffer = ref new HoloLensForCV::MultiFrameBuffer();
	_depthFrameBuffer = ref new HoloLensForCV::MultiFrameBuffer();

	StartHoloLensMediaFrameSourceGroup();
	return 0;
}

void FaceDetector::Dispose()
{
	dbg::trace(L"Stopping the MediaFrameSourceGroups");
	if (_rgbFrameSourceGroup == nullptr && _depthFrameSourceGroup == nullptr)
		return;

	concurrency::create_task(_rgbFrameSourceGroup->StopAsync()).then(
		[&]()
	{
		delete _rgbFrameSourceGroup;
		_rgbFrameSourceGroup = nullptr;
		_rgbFrameSourceGroupStarted = false;
	}).wait();

	concurrency::create_task(_depthFrameSourceGroup->StopAsync()).then(
		[&]()
	{
		delete _depthFrameSourceGroup;
		_depthFrameSourceGroup = nullptr;
		_depthFrameSourceGroupStarted = false;
	}).wait();
	dbg::trace(L"MediaFrameSourceGroups have stopped");
}

bool FaceDetector::DetectFace()
{
	if (!_rgbFrameSourceGroupStarted)
	{
		dbg::trace(L"RGB MediaFrameSourceGroup not yet started");
		return false;
	}
	dbg::trace(L"Obtaining most recent PhotoVideo frame");
	HoloLensForCV::SensorFrame^ frame = _rgbFrameBuffer->GetLatestFrame(HoloLensForCV::SensorType::PhotoVideo);
	if (!frame)
	{
		dbg::trace(L"No frame was obtained");
		return false;
	}
	dbg::trace(L"Detecting markers");
	_detectedFace = DetectFaceCascade(_unitySpatialCoordinateSystem, frame);

	return true;
}

DetectedFace FaceDetector::DetectFaceCascade(SpatialCoordinateSystem^ unityCoordinateSystem, HoloLensForCV::SensorFrame^ frame)
{
	DetectedFace detectedFace;
	cv::Mat matRgbImg, grayImg, smallImg;
	std::vector<cv::Rect> faces;

	rmcv::WrapHoloLensSensorFrameWithCvMat(frame, matRgbImg);

	// convert to grayscale and rescale to reduce computation time
	cv::cvtColor(matRgbImg, grayImg, cv::COLOR_BGRA2GRAY);
	double scale = 4;
	double fx = 1 / scale; 
	cv::resize(grayImg, smallImg, cv::Size(), fx, fx, cv::INTER_LINEAR);
	cv::equalizeHist(smallImg, smallImg);

	dbg::trace(L"Starting detection algorithm");

	_faceCascade.detectMultiScale(smallImg, faces, 1.1, 2, 0 | cv::CASCADE_SCALE_IMAGE, cv::Size(30, 30));

	dbg::trace(L"Detection complete. Found %i faces", faces.size());

	if (!faces.empty())
	{
		cv::Rect faceSmall = faces[0];
		cv::Rect faceRect(cv::Point(cvRound(faceSmall.x * scale), cvRound(faceSmall.y  * scale)),
			cv::Point(cvRound((faceSmall.x + faceSmall.width - 1) * scale), cvRound((faceSmall.y + faceSmall.height - 1) * scale)));
		
		detectedFace.x = faceRect.x;
		detectedFace.y = faceRect.y;
		detectedFace.width = faceRect.width;
		detectedFace.height = faceRect.height;

		IBox<float4x4>^ cameraToUnityReference = frame->FrameCoordinateSystem->TryGetTransformTo(unityCoordinateSystem);
		if (!cameraToUnityReference)
		{
			dbg::trace(L"Failed to obtain transform to unity coordinate space");
			throw ref new FailureException();
		}

		float4x4 cameraToUnity = cameraToUnityReference->Value;

		float4x4 viewToCamera;
		invert(frame->CameraViewTransform, &viewToCamera);

		// Winrt uses right-handed row-vector representation for transforms
		// Unity uses left-handed column vector representation for transforms
		// Therefore, we convert our original winrt 'transform' to the 'transformUnity' by transposing and flipping z values
		float4x4 transform = viewToCamera * cameraToUnity;
		float4x4 transformUnity = transpose(transform);
		transformUnity.m31 *= -1.0f;
		transformUnity.m32 *= -1.0f;
		transformUnity.m33 *= -1.0f;
		transformUnity.m34 *= -1.0f;

		detectedFace.cameraToWorldUnity = transformUnity;
	}

	return detectedFace;
}

void FaceDetector::StartHoloLensMediaFrameSourceGroup()
{
	dbg::trace(L"Starting the MediaFrameSourceGroups");
	REQUIRES(
		!_rgbFrameSourceGroupStarted &&
		!_depthFrameSourceGroupStarted &&
		nullptr != _spatialPerception);

	_rgbFrameSourceGroup =
		ref new HoloLensForCV::MediaFrameSourceGroup(
			HoloLensForCV::MediaFrameSourceGroupType::PhotoVideoCamera,
			_spatialPerception, _rgbFrameBuffer);

	_depthFrameSourceGroup =
		ref new HoloLensForCV::MediaFrameSourceGroup(
			HoloLensForCV::MediaFrameSourceGroupType::HoloLensResearchModeSensors,
			_spatialPerception, _depthFrameBuffer);

	for (const auto enabledSensorType : _enabledSensorTypes)
	{
		if (enabledSensorType == HoloLensForCV::SensorType::PhotoVideo)
		{
			_rgbFrameSourceGroup->Enable(enabledSensorType);
		}
		else
		{
			_depthFrameSourceGroup->Enable(enabledSensorType);
		}
	}

	// Start the media frame source groups.
	auto startPhotoVideoMediaFrameSourceGroupTask =
		concurrency::create_task(
			_rgbFrameSourceGroup->StartAsync());

	startPhotoVideoMediaFrameSourceGroupTask.then([&]() {
		_rgbFrameSourceGroupStarted = true;
	});

	auto startReseachModeMediaFrameSourceGroupTask =
		concurrency::create_task(
			_depthFrameSourceGroup->StartAsync());

	startReseachModeMediaFrameSourceGroupTask.then([&]() {
		_depthFrameSourceGroupStarted = true;
	});
	dbg::trace(L"MediaFrameSourceGroups have started");
}
