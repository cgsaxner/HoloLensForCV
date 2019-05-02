#include "pch.h"
#include "FaceDetectionPlugin.h"
#include "FaceDetector.h"

using namespace FaceDetectorPlugin;

FaceDetector* faceDetector = nullptr;

extern "C" __declspec(dllexport) bool __stdcall CheckPlugin()
{
	return true;
}

extern "C" __declspec(dllexport) void __stdcall Initialize(int* spatialCoordinateSystem)
{
	if (!faceDetector)
	{
		faceDetector = new FaceDetector();
	}

	faceDetector->Initialize(spatialCoordinateSystem);
}

extern "C" __declspec(dllexport) void __stdcall Dispose()
{
	if (faceDetector)
	{
		faceDetector->Dispose();
	}
}

extern "C" __declspec(dllexport) bool __stdcall DetectFaces()
{
	if (faceDetector)
	{
		return faceDetector->DetectFace();
	}
	return false;
}
