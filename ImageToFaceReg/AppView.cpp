#include "pch.h"

#include "ImageToFaceRegMain.h"
#include "AppView.h"

// The main function is only used to initialize our IFrameworkView class.
// Under most circumstances, you should not need to modify this function.
[Platform::MTAThread]
int main(
	Platform::Array<Platform::String^>^)
{
	ImageToFaceReg::AppViewSource^ appViewSource =
		ref new ImageToFaceReg::AppViewSource();

	Windows::ApplicationModel::Core::CoreApplication::Run(
		appViewSource);

	return 0;
}

namespace ImageToFaceReg
{
	Windows::ApplicationModel::Core::IFrameworkView^ AppViewSource::CreateView()
	{
		return ref new AppView();
	}

	AppView::AppView()
	{
	}

	std::shared_ptr<Holographic::AppMainBase> AppView::InitializeCore()
	{
		return std::make_shared<AppMain>(
			_deviceResources);
	}
}

