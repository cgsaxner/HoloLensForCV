#pragma once

namespace ImageToFaceReg
{
	// IFrameworkView class. Connects the app with the Windows shell and handles application lifecycle events.
	ref class AppView sealed : public Holographic::AppViewBase
	{
	public:
		AppView();

	protected private:
		virtual std::shared_ptr<Holographic::AppMainBase> InitializeCore() override;
	};

	// The entry point for the app.
	ref class AppViewSource sealed : Windows::ApplicationModel::Core::IFrameworkViewSource
	{
	public:
		virtual Windows::ApplicationModel::Core::IFrameworkView^ CreateView();
	};
}
