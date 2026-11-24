// Copyright (c) REAN Studios. MIT License.
//
// Editor module: checks the client id in Project Settings > Plugins > REAN SDK,
// adds a "Test sign-in" button there, and the console command Rean.TestSignIn.
#include "ReanEditorSignInTest.h"
#include "ReanSettings.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "HAL/IConsoleManager.h"
#include "IDetailCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ReanSDKEditor"

DEFINE_LOG_CATEGORY_STATIC(LogReanSDKEditor, Log, All);

class FReanSDKEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditor.RegisterCustomClassLayout(UReanSettings::StaticClass()->GetFName(),
			FOnGetDetailCustomizationInstance::CreateStatic(&FReanSDKEditorModule::MakeCustomization));
		PropertyEditor.NotifyCustomizationModuleChanged();

		TestCommand = IConsoleManager::Get().RegisterConsoleCommand(TEXT("Rean.TestSignIn"),
			TEXT("Runs a REAN device-code sign-in with the client id from Project Settings and prints the player."),
			FConsoleCommandDelegate::CreateStatic(&FReanSDKEditorModule::StartTest), ECVF_Default);

		const FString Problem = GetDefault<UReanSettings>()->ValidateClientId();
		if (!Problem.IsEmpty())
		{
			UE_LOG(LogReanSDKEditor, Warning, TEXT("REAN SDK (Project Settings > Plugins > REAN SDK): %s"), *Problem);
		}
	}

	virtual void ShutdownModule() override
	{
		if (TestCommand)
		{
			IConsoleManager::Get().UnregisterConsoleObject(TestCommand);
			TestCommand = nullptr;
		}
		if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
		{
			FPropertyEditorModule& PropertyEditor = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
			PropertyEditor.UnregisterCustomClassLayout(UReanSettings::StaticClass()->GetFName());
		}
		Test().Reset();
	}

	static void StartTest()
	{
		TSharedPtr<FReanEditorSignInTest>& Current = Test();
		if (Current.IsValid() && Current->IsRunning()) return;
		Current = MakeShared<FReanEditorSignInTest>();
		Current->Start();
	}

private:
	IConsoleObject* TestCommand = nullptr;

	static TSharedPtr<FReanEditorSignInTest>& Test()
	{
		static TSharedPtr<FReanEditorSignInTest> Instance;
		return Instance;
	}

	class FSettingsCustomization : public IDetailCustomization
	{
	public:
		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(TEXT("Test"), LOCTEXT("TestCategory", "Test"), ECategoryPriority::Important);
			Category.AddCustomRow(LOCTEXT("ClientIdCheck", "Client id check"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("ClientIdLabel", "Client id"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(300.0f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.Text_Lambda([]() {
						const FString Problem = GetDefault<UReanSettings>()->ValidateClientId();
						return Problem.IsEmpty() ? LOCTEXT("ClientIdOk", "Looks right.") : FText::FromString(Problem);
					})
				];
			Category.AddCustomRow(LOCTEXT("TestSignInRow", "Test sign-in"))
				.NameContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("TestSignInLabel", "Test sign-in"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.ValueContent()
				.MinDesiredWidth(300.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("TestSignInButton", "Sign in with a device code"))
					.ToolTipText(LOCTEXT("TestSignInTip", "Checks your app without running the game: shows a code to enter at reanstudios.com/activate, then prints the player. Nothing is saved."))
					.OnClicked_Lambda([]() {
						FReanSDKEditorModule::StartTest();
						return FReply::Handled();
					})
				];
		}
	};

	static TSharedRef<IDetailCustomization> MakeCustomization()
	{
		return MakeShared<FSettingsCustomization>();
	}
};

IMPLEMENT_MODULE(FReanSDKEditorModule, ReanSDKEditor)

#undef LOCTEXT_NAMESPACE
