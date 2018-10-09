//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2018 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef UI_DIALOG_HXX
#define UI_DIALOG_HXX

class CommandSender;
class Dialog;
class DialogContainer;
class CheckboxWidget;
class PopUpWidget;
class SliderWidget;
class StaticTextWidget;
class TabWidget;
class BrowserDialog;
class OSystem;

#include "bspf.hxx"

class UIDialog : public Dialog
{
  public:
    UIDialog(OSystem& osystem, DialogContainer& parent, const GUI::Font& font);
    virtual ~UIDialog();

  private:
    void loadConfig() override;
    void saveConfig() override;
    void setDefaults() override;

    void handleCommand(CommandSender* sender, int cmd, int data, int id) override;
    void handleRomViewer();
    void createBrowser(const string& title);

  private:
    enum
    {
      kListDelay  = 'UILd',
      kMouseWheel = 'UIMw',
      kLauncherSize = 'UIls',
      kRomViewer = 'UIRv',
      kChooseSnapLoadDirCmd = 'UIsl', // snapshot dir (load files)
      kSnapLoadDirChosenCmd = 'UIsc' // snap chosen (load files)
    };

    const GUI::Font& myFont;
    TabWidget* myTab;

    // Launcher options
    SliderWidget*     myLauncherWidthSlider;
    SliderWidget*     myLauncherHeightSlider;
    PopUpWidget*      myLauncherFontPopup;
    PopUpWidget*      myRomViewerPopup;
    ButtonWidget*     myOpenBrowserButton;
    EditTextWidget*   mySnapLoadPath;
    CheckboxWidget*   myLauncherExitWidget;

    // Misc options
    PopUpWidget*      myPalettePopup;
    SliderWidget*     myListDelayPopup;
    SliderWidget*     myWheelLinesPopup;

    unique_ptr<BrowserDialog> myBrowser;

  private:
    // Following constructors and assignment operators not supported
    UIDialog() = delete;
    UIDialog(const UIDialog&) = delete;
    UIDialog(UIDialog&&) = delete;
    UIDialog& operator=(const UIDialog&) = delete;
    UIDialog& operator=(UIDialog&&) = delete;
};

#endif
