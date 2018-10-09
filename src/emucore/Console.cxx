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

#include <cassert>
#include <stdexcept>

#include "AtariVox.hxx"
#include "Booster.hxx"
#include "Cart.hxx"
#include "Control.hxx"
#include "Cart.hxx"
#include "Driving.hxx"
#include "Event.hxx"
#include "EventHandler.hxx"
#include "Joystick.hxx"
#include "Keyboard.hxx"
#include "KidVid.hxx"
#include "Genesis.hxx"
#include "MindLink.hxx"
#include "CompuMate.hxx"
#include "M6502.hxx"
#include "M6532.hxx"
#include "TIA.hxx"
#include "Paddles.hxx"
#include "Props.hxx"
#include "PropsSet.hxx"
#include "SaveKey.hxx"
#include "Settings.hxx"
#include "Sound.hxx"
#include "Switches.hxx"
#include "System.hxx"
#include "AmigaMouse.hxx"
#include "AtariMouse.hxx"
#include "TrakBall.hxx"
#include "FrameBuffer.hxx"
#include "TIASurface.hxx"
#include "OSystem.hxx"
#include "Menu.hxx"
#include "CommandMenu.hxx"
#include "Serializable.hxx"
#include "Serializer.hxx"
#include "Version.hxx"
#include "TIAConstants.hxx"
#include "FrameLayout.hxx"
#include "AudioQueue.hxx"
#include "AudioSettings.hxx"
#include "frame-manager/FrameManager.hxx"
#include "frame-manager/FrameLayoutDetector.hxx"
#include "frame-manager/YStartDetector.hxx"

#ifdef DEBUGGER_SUPPORT
  #include "Debugger.hxx"
#endif

#ifdef CHEATCODE_SUPPORT
  #include "CheatManager.hxx"
#endif

#include "Console.hxx"

namespace {
  constexpr uInt8 YSTART_EXTRA = 2;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Console::Console(OSystem& osystem, unique_ptr<Cartridge>& cart,
                 const Properties& props, AudioSettings& audioSettings)
  : myOSystem(osystem),
    myEvent(osystem.eventHandler().event()),
    myProperties(props),
    myCart(std::move(cart)),
    myDisplayFormat(""),  // Unknown TV format @ start
    myCurrentFormat(0),   // Unknown format @ start,
    myAutodetectedYstart(0),
    myYStartAutodetected(false),
    myUserPaletteDefined(false),
    myConsoleTiming(ConsoleTiming::ntsc),
    myAudioSettings(audioSettings)
{
  // Load user-defined palette for this ROM
  loadUserPalette();

  // Create subsystems for the console
  my6502 = make_unique<M6502>(myOSystem.settings());
  myRiot = make_unique<M6532>(*this, myOSystem.settings());
  myTIA  = make_unique<TIA>(*this, myOSystem.settings());
  myFrameManager = make_unique<FrameManager>();
  mySwitches = make_unique<Switches>(myEvent, myProperties, myOSystem.settings());

  myTIA->setFrameManager(myFrameManager.get());

  // Construct the system and components
  mySystem = make_unique<System>(osystem, *my6502, *myRiot, *myTIA, *myCart);

  // The real controllers for this console will be added later
  // For now, we just add dummy joystick controllers, since autodetection
  // runs the emulation for a while, and this may interfere with 'smart'
  // controllers such as the AVox and SaveKey
  myLeftControl  = make_unique<Joystick>(Controller::Left, myEvent, *mySystem);
  myRightControl = make_unique<Joystick>(Controller::Right, myEvent, *mySystem);

  // Let the cart know how to query for the 'Cartridge.StartBank' property
  myCart->setStartBankFromPropsFunc([this]() {
    const string& startbank = myProperties.get(Cartridge_StartBank);
    return startbank == EmptyString ? -1 : atoi(startbank.c_str());
  });

  // We can only initialize after all the devices/components have been created
  mySystem->initialize();

  // Auto-detect NTSC/PAL mode if it's requested
  string autodetected = "";
  myDisplayFormat = myProperties.get(Display_Format);

  // Add the real controllers for this system
  // This must be done before the debugger is initialized
  const string& md5 = myProperties.get(Cartridge_MD5);
  setControllers(md5);

  // Mute audio and clear framebuffer while autodetection runs
  myOSystem.sound().mute(1);
  myOSystem.frameBuffer().clear();

  if(myDisplayFormat == "AUTO" || myOSystem.settings().getBool("rominfo"))
  {
    autodetectFrameLayout();

    if(myProperties.get(Display_Format) == "AUTO")
    {
      autodetected = "*";
      myCurrentFormat = 0;
    }
  }

  if (atoi(myProperties.get(Display_YStart).c_str()) == 0) {
    autodetectYStart();
  }

  myConsoleInfo.DisplayFormat = myDisplayFormat + autodetected;

  // Set up the correct properties used when toggling format
  // Note that this can be overridden if a format is forced
  //   For example, if a PAL ROM is forced to be NTSC, it will use NTSC-like
  //   properties (60Hz, 262 scanlines, etc), but likely result in flicker
  if(myDisplayFormat == "NTSC")
  {
    myCurrentFormat = 1;
    myConsoleTiming = ConsoleTiming::ntsc;
  }
  else if(myDisplayFormat == "PAL")
  {
    myCurrentFormat = 2;
    myConsoleTiming = ConsoleTiming::pal;
  }
  else if(myDisplayFormat == "SECAM")
  {
    myCurrentFormat = 3;
    myConsoleTiming = ConsoleTiming::secam;
  }
  else if(myDisplayFormat == "NTSC50")
  {
    myCurrentFormat = 4;
    myConsoleTiming = ConsoleTiming::ntsc;
  }
  else if(myDisplayFormat == "PAL60")
  {
    myCurrentFormat = 5;
    myConsoleTiming = ConsoleTiming::pal;
  }
  else if(myDisplayFormat == "SECAM60")
  {
    myCurrentFormat = 6;
    myConsoleTiming = ConsoleTiming::secam;
  }

  setTIAProperties();

  bool joyallow4 = myOSystem.settings().getBool("joyallow4");
  myOSystem.eventHandler().allowAllDirections(joyallow4);

  // Reset the system to its power-on state
  mySystem->reset();

  // Finally, add remaining info about the console
  myConsoleInfo.CartName   = myProperties.get(Cartridge_Name);
  myConsoleInfo.CartMD5    = myProperties.get(Cartridge_MD5);
  bool swappedPorts = properties().get(Console_SwapPorts) == "YES";
  myConsoleInfo.Control0   = myLeftControl->about(swappedPorts);
  myConsoleInfo.Control1   = myRightControl->about(swappedPorts);
  myConsoleInfo.BankSwitch = myCart->about();

  myCart->setRomName(myConsoleInfo.CartName);

  // Let the other devices know about the new console
  mySystem->consoleChanged(myConsoleTiming);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Console::~Console()
{
  // Some smart controllers need to be informed that the console is going away
  myLeftControl->close();
  myRightControl->close();

  // Close audio to prevent invalid access to myConsoleTiming from the audio
  // callback
  myOSystem.sound().close();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::autodetectFrameLayout(bool reset)
{
  // Run the TIA, looking for PAL scanline patterns
  // We turn off the SuperCharger progress bars, otherwise the SC BIOS
  // will take over 250 frames!
  // The 'fastscbios' option must be changed before the system is reset
  bool fastscbios = myOSystem.settings().getBool("fastscbios");
  myOSystem.settings().setValue("fastscbios", true);

  FrameLayoutDetector frameLayoutDetector;
  myTIA->setFrameManager(&frameLayoutDetector);

  if (reset) mySystem->reset(true);

  for(int i = 0; i < 60; ++i) myTIA->update();

  myTIA->setFrameManager(myFrameManager.get());

  myDisplayFormat = frameLayoutDetector.detectedLayout() == FrameLayout::pal ? "PAL" : "NTSC";

  // Don't forget to reset the SC progress bars again
  myOSystem.settings().setValue("fastscbios", fastscbios);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::redetectFrameLayout()
{
  Serializer s;

  myOSystem.sound().close();
  save(s);

  autodetectFrameLayout(false);
  if (myYStartAutodetected) autodetectYStart();

  load(s);
  initializeAudio();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::autodetectYStart(bool reset)
{
  // We turn off the SuperCharger progress bars, otherwise the SC BIOS
  // will take over 250 frames!
  // The 'fastscbios' option must be changed before the system is reset
  bool fastscbios = myOSystem.settings().getBool("fastscbios");
  myOSystem.settings().setValue("fastscbios", true);

  YStartDetector ystartDetector;
  ystartDetector.setLayout(myDisplayFormat == "PAL" ? FrameLayout::pal : FrameLayout::ntsc);
  myTIA->setFrameManager(&ystartDetector);

  if (reset) mySystem->reset(true);

  for (int i = 0; i < 80; i++) myTIA->update();

  myTIA->setFrameManager(myFrameManager.get());

  myAutodetectedYstart = ystartDetector.detectedYStart() - YSTART_EXTRA;

  // Don't forget to reset the SC progress bars again
  myOSystem.settings().setValue("fastscbios", fastscbios);

  myYStartAutodetected = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::redetectYStart()
{
  Serializer s;

  myOSystem.sound().close();
  save(s);

  autodetectYStart(false);

  load(s);
  initializeAudio();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Console::save(Serializer& out) const
{
  try
  {
    // First save state for the system
    if(!mySystem->save(out))
      return false;

    // Now save the console controllers and switches
    if(!(myLeftControl->save(out) && myRightControl->save(out) &&
         mySwitches->save(out)))
      return false;
  }
  catch(...)
  {
    cerr << "ERROR: Console::save" << endl;
    return false;
  }

  return true;  // success
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Console::load(Serializer& in)
{
  try
  {
    // First load state for the system
    if(!mySystem->load(in))
      return false;

    // Then load the console controllers and switches
    if(!(myLeftControl->load(in) && myRightControl->load(in) &&
         mySwitches->load(in)))
      return false;
  }
  catch(...)
  {
    cerr << "ERROR: Console::load" << endl;
    return false;
  }

  return true;  // success
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleFormat(int direction)
{
  string saveformat, message;
  uInt32 format = myCurrentFormat;

  if(direction == 1)
    format = (myCurrentFormat + 1) % 7;
  else if(direction == -1)
    format = myCurrentFormat > 0 ? (myCurrentFormat - 1) : 6;

  setFormat(format);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setFormat(uInt32 format)
{
  if(myCurrentFormat == format)
    return;

  string saveformat, message;
  string autodetected = "";
  bool reset = true;

  myCurrentFormat = format;
  switch(myCurrentFormat)
  {
    case 0:  // auto-detect
    {
      string oldDisplayFormat = myDisplayFormat;
      redetectFrameLayout();
      myTIA->update();
      reset = oldDisplayFormat != myDisplayFormat;
      saveformat = "AUTO";
      autodetected = "*";
      myConsoleTiming = myDisplayFormat == "PAL" ? ConsoleTiming::pal : ConsoleTiming::ntsc;
      message = "Auto-detect mode: " + myDisplayFormat;
      break;
    }
    case 1:
      saveformat = myDisplayFormat = "NTSC";
      myConsoleTiming = ConsoleTiming::ntsc;
      message = "NTSC mode";
      break;
    case 2:
      saveformat = myDisplayFormat = "PAL";
      myConsoleTiming = ConsoleTiming::pal;
      message = "PAL mode";
      break;
    case 3:
      saveformat = myDisplayFormat = "SECAM";
      myConsoleTiming = ConsoleTiming::secam;
      message = "SECAM mode";
      break;
    case 4:
      saveformat = myDisplayFormat = "NTSC50";
      myConsoleTiming = ConsoleTiming::ntsc;
      message = "NTSC50 mode";
      break;
    case 5:
      saveformat = myDisplayFormat = "PAL60";
      myConsoleTiming = ConsoleTiming::pal;
      message = "PAL60 mode";
      break;
    case 6:
      saveformat = myDisplayFormat = "SECAM60";
      myConsoleTiming = ConsoleTiming::secam;
      message = "SECAM60 mode";
      break;
  }
  myProperties.set(Display_Format, saveformat);

  myConsoleInfo.DisplayFormat = myDisplayFormat + autodetected;

  if(reset)
  {
    setPalette(myOSystem.settings().getString("palette"));
    setTIAProperties();
    initializeVideo(); // takes care of refreshing the screen
    initializeAudio(); // ensure that audio synthesis is set up to match emulation speed
    myOSystem.resetFps(); // Reset FPS measurement
  }

  myOSystem.frameBuffer().showMessage(message);

  // Let the other devices know about the console change
  mySystem->consoleChanged(myConsoleTiming);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleColorLoss()
{
  bool colorloss = !myTIA->colorLossEnabled();
  if(myTIA->enableColorLoss(colorloss))
  {
    myOSystem.settings().setValue(
      myOSystem.settings().getBool("dev.settings") ? "dev.colorloss" : "plr.colorloss", colorloss);

    string message = string("PAL color-loss ") +
                     (colorloss ? "enabled" : "disabled");
    myOSystem.frameBuffer().showMessage(message);
  }
  else
    myOSystem.frameBuffer().showMessage(
      "PAL color-loss not available in non PAL modes");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::enableColorLoss(bool state)
{
  myTIA->enableColorLoss(state);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::togglePalette()
{
  string palette, message;
  palette = myOSystem.settings().getString("palette");

  if(palette == "standard")       // switch to z26
  {
    palette = "z26";
    message = "Z26 palette";
  }
  else if(palette == "z26")       // switch to user or standard
  {
    // If we have a user-defined palette, it will come next in
    // the sequence; otherwise loop back to the standard one
    if(myUserPaletteDefined)
    {
      palette = "user";
      message = "User-defined palette";
    }
    else
    {
      palette = "standard";
      message = "Standard Stella palette";
    }
  }
  else if(palette == "user")  // switch to standard
  {
    palette = "standard";
    message = "Standard Stella palette";
  }
  else  // switch to standard mode if we get this far
  {
    palette = "standard";
    message = "Standard Stella palette";
  }

  myOSystem.settings().setValue("palette", palette);
  myOSystem.frameBuffer().showMessage(message);

  setPalette(palette);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setPalette(const string& type)
{
  // Look at all the palettes, since we don't know which one is
  // currently active
  static uInt32* palettes[3][3] = {
    { &ourNTSCPalette[0],     &ourPALPalette[0],     &ourSECAMPalette[0]     },
    { &ourNTSCPaletteZ26[0],  &ourPALPaletteZ26[0],  &ourSECAMPaletteZ26[0]  },
    { &ourUserNTSCPalette[0], &ourUserPALPalette[0], &ourUserSECAMPalette[0] }
  };

  // See which format we should be using
  int paletteNum = 0;
  if(type == "standard")
    paletteNum = 0;
  else if(type == "z26")
    paletteNum = 1;
  else if(type == "user" && myUserPaletteDefined)
    paletteNum = 2;

  // Now consider the current display format
  const uInt32* palette =
    (myDisplayFormat.compare(0, 3, "PAL") == 0)   ? palettes[paletteNum][1] :
    (myDisplayFormat.compare(0, 5, "SECAM") == 0) ? palettes[paletteNum][2] :
     palettes[paletteNum][0];

  myOSystem.frameBuffer().setPalette(palette);

  if(myTIA->usingFixedColors())
    myTIA->enableFixedColors(true);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::togglePhosphor()
{
  if(myOSystem.frameBuffer().tiaSurface().phosphorEnabled())
  {
    myProperties.set(Display_Phosphor, "No");
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(false);
    myOSystem.frameBuffer().showMessage("Phosphor effect disabled");
  }
  else
  {
    myProperties.set(Display_Phosphor, "Yes");
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(true);
    myOSystem.frameBuffer().showMessage("Phosphor effect enabled");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changePhosphor(int direction)
{
  int blend = atoi(myProperties.get(Display_PPBlend).c_str());

  if(myOSystem.frameBuffer().tiaSurface().phosphorEnabled())
  {
    if(direction == +1)       // increase blend
    {
      if(blend >= 100)
      {
        myOSystem.frameBuffer().showMessage("Phosphor blend at maximum");
        return;
      }
      else
        blend = std::min(blend+2, 100);
    }
    else if(direction == -1)  // decrease blend
    {
      if(blend <= 2)
      {
        myOSystem.frameBuffer().showMessage("Phosphor blend at minimum");
        return;
      }
      else
        blend = std::max(blend-2, 0);
    }
    else
      return;

    ostringstream val;
    val << blend;
    myProperties.set(Display_PPBlend, val.str());
    myOSystem.frameBuffer().showMessage("Phosphor blend " + val.str());
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(true, blend);
  }
  else
    myOSystem.frameBuffer().showMessage("Phosphor effect disabled");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setProperties(const Properties& props)
{
  myProperties = props;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus Console::initializeVideo(bool full)
{
  FBInitStatus fbstatus = FBInitStatus::Success;

  if(full)
  {
    bool devSettings = myOSystem.settings().getBool("dev.settings");
    const string& title = string("Stella ") + STELLA_VERSION +
                   ": \"" + myProperties.get(Cartridge_Name) + "\"";
    fbstatus = myOSystem.frameBuffer().createDisplay(title,
                 myTIA->width() << 1, myTIA->height());
    if(fbstatus != FBInitStatus::Success)
      return fbstatus;

    myOSystem.frameBuffer().showFrameStats(
      myOSystem.settings().getBool(devSettings ? "dev.stats" : "plr.stats"));
    generateColorLossPalette();
  }
  setPalette(myOSystem.settings().getString("palette"));

  return fbstatus;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::initializeAudio()
{
  myOSystem.sound().close();

  myEmulationTiming
    .updatePlaybackRate(myAudioSettings.sampleRate())
    .updatePlaybackPeriod(myAudioSettings.fragmentSize())
    .updateAudioQueueExtraFragments(myAudioSettings.bufferSize())
    .updateAudioQueueHeadroom(myAudioSettings.headroom())
    .updateSpeedFactor(myOSystem.settings().getFloat("speed"));

  createAudioQueue();
  myTIA->setAudioQueue(myAudioQueue);

  myOSystem.sound().open(myAudioQueue, &myEmulationTiming);
}

/* Original frying research and code by Fred Quimby.
   I've tried the following variations on this code:
   - Both OR and Exclusive OR instead of AND. This generally crashes the game
     without ever giving us realistic "fried" effects.
   - Loop only over the RIOT RAM. This still gave us frying-like effects, but
     it seemed harder to duplicate most effects. I have no idea why, but
     munging the TIA regs seems to have some effect (I'd think it wouldn't).

   Fred says he also tried mangling the PC and registers, but usually it'd just
   crash the game (e.g. black screen, no way out of it).

   It's definitely easier to get some effects (e.g. 255 lives in Battlezone)
   with this code than it is on a real console. My guess is that most "good"
   frying effects come from a RIOT location getting cleared to 0. Fred's
   code is more likely to accomplish this than frying a real console is...

   Until someone comes up with a more accurate way to emulate frying, I'm
   leaving this as Fred posted it.   -- B.
*/
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::fry() const
{
  for(int i = 0; i < 0x100; i += mySystem->randGenerator().next() % 4)
    mySystem->poke(i, mySystem->peek(i) & mySystem->randGenerator().next());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changeYStart(int direction)
{
  uInt32 ystart = myTIA->ystart();

  if(direction == +1)       // increase YStart
  {
    if(ystart >= TIAConstants::maxYStart)
    {
      myOSystem.frameBuffer().showMessage("YStart at maximum");
      return;
    }

    ++ystart;
    myYStartAutodetected = false;
  }
  else if(direction == -1)  // decrease YStart
  {
    if(ystart == 0)
    {
      throw runtime_error("cannot happen");
    }

    --ystart;
    myYStartAutodetected = false;
  }
  else
    return;

  ostringstream val;
  val << ystart;
  if(ystart == 0) {
    redetectYStart();
    ystart = myAutodetectedYstart;

    myOSystem.frameBuffer().showMessage("YStart autodetected");
  }
  else
  {
    if(myAutodetectedYstart > 0 && myAutodetectedYstart == ystart)
    {
      // We've reached the auto-detect value, so reset
      myOSystem.frameBuffer().showMessage("YStart " + val.str() + " (Auto)");
      val.str("");
      val << static_cast<int>(0);
    }
    else
      myOSystem.frameBuffer().showMessage("YStart " + val.str());

    myYStartAutodetected = false;
  }

  myProperties.set(Display_YStart, val.str());
  myTIA->setYStart(ystart);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::updateYStart(uInt32 ystart)
{
  if (ystart > TIAConstants::maxYStart) return;

  ostringstream ss;
  ss << ystart;

  if (ss.str() == myProperties.get(Display_YStart)) return;

  myProperties.set(Display_YStart, ss.str());

  if (ystart == 0) {
    redetectYStart();
    myTIA->setYStart(myAutodetectedYstart);
  } else {
    myTIA->setYStart(ystart);
    myYStartAutodetected = false;
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changeHeight(int direction)
{
  uInt32 height = myTIA->height();
  uInt32 dheight = myOSystem.frameBuffer().desktopSize().h;

  if(direction == +1)       // increase Height
  {
    ++height;
    if(height > TIAConstants::maxViewableHeight || height > dheight)
    {
      myOSystem.frameBuffer().showMessage("Height at maximum");
      return;
    }
  }
  else if(direction == -1)  // decrease Height
  {
    --height;
    if(height < TIAConstants::minViewableHeight) height = 0;
  }
  else
    return;

  myTIA->setHeight(height);
  initializeVideo();  // takes care of refreshing the screen

  ostringstream val;
  val << height;
  myOSystem.frameBuffer().showMessage("Height " + val.str());
  myProperties.set(Display_Height, val.str());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setTIAProperties()
{
  uInt32 ystart = atoi(myProperties.get(Display_YStart).c_str());
  if(ystart != 0)
    ystart = BSPF::clamp(ystart, 0u, TIAConstants::maxYStart);
  uInt32 height = atoi(myProperties.get(Display_Height).c_str());
  if(height != 0)
    height = BSPF::clamp(height, TIAConstants::minViewableHeight, TIAConstants::maxViewableHeight);

  if(myDisplayFormat == "NTSC" || myDisplayFormat == "PAL60" ||
     myDisplayFormat == "SECAM60")
  {
    // Assume we've got ~262 scanlines (NTSC-like format)
    myTIA->setLayout(FrameLayout::ntsc);
  }
  else
  {
    // Assume we've got ~312 scanlines (PAL-like format)
    // PAL ROMs normally need at least 250 lines
    if (height != 0) height = std::max(height, 250u);

    myTIA->setLayout(FrameLayout::pal);
  }

  myTIA->setYStart(myAutodetectedYstart ? myAutodetectedYstart : ystart);
  myTIA->setHeight(height);

  myEmulationTiming.updateFrameLayout(myTIA->frameLayout());
  myEmulationTiming.updateConsoleTiming(myConsoleTiming);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::createAudioQueue()
{
  const string& stereo = myOSystem.settings().getString(AudioSettings::SETTING_STEREO);
  bool useStereo = false;
  if(BSPF::equalsIgnoreCase(stereo, "byrom"))
    useStereo = myProperties.get(Cartridge_Sound) == "STEREO";
  else
    useStereo = BSPF::equalsIgnoreCase(stereo, "stereo");

  myAudioQueue = make_shared<AudioQueue>(
    myEmulationTiming.audioFragmentSize(),
    myEmulationTiming.audioQueueCapacity(),
    useStereo
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setControllers(const string& rommd5)
{
  // Check for CompuMate scheme; it is special in that a handler creates both
  // controllers for us, and associates them with the bankswitching class
  if(myCart->detectedType() == "CM")
  {
    myCMHandler = make_shared<CompuMate>(*this, myEvent, *mySystem);

    // A somewhat ugly bit of code that casts to CartridgeCM to
    // add the CompuMate, and then back again for the actual
    // Cartridge
    unique_ptr<CartridgeCM> cartcm(static_cast<CartridgeCM*>(myCart.release()));
    cartcm->setCompuMate(myCMHandler);
    myCart = std::move(cartcm);

    myLeftControl  = std::move(myCMHandler->leftController());
    myRightControl = std::move(myCMHandler->rightController());
  }
  else
  {
    // Setup the controllers based on properties
    const string& left  = myProperties.get(Controller_Left);
    const string& right = myProperties.get(Controller_Right);

    unique_ptr<Controller> leftC = getControllerPort(rommd5, left, Controller::Left),
      rightC = getControllerPort(rommd5, right, Controller::Right);

    // Swap the ports if necessary
    if(myProperties.get(Console_SwapPorts) == "NO")
    {
      myLeftControl = std::move(leftC);
      myRightControl = std::move(rightC);
    }
    else
    {
      myLeftControl = std::move(rightC);
      myRightControl = std::move(leftC);
    }
  }

  myTIA->bindToControllers();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
unique_ptr<Controller> Console::getControllerPort(const string& rommd5,
    const string& controllerName, Controller::Jack port)
{
  unique_ptr<Controller> controller = std::move(myLeftControl);

  if(controllerName == "JOYSTICK")
  {
    // Already created in c'tor
    // We save some time by not looking at all the other types
    if(!controller)
      controller = make_unique<Joystick>(port, myEvent, *mySystem);
  }
  else if(controllerName == "BOOSTERGRIP")
  {
    controller = make_unique<BoosterGrip>(port, myEvent, *mySystem);
  }
  else if(controllerName == "DRIVING")
  {
    controller = make_unique<Driving>(port, myEvent, *mySystem);
  }
  else if((controllerName == "KEYBOARD") || (controllerName == "KEYPAD"))
  {
    controller = make_unique<Keyboard>(port, myEvent, *mySystem);
  }
  else if(BSPF::startsWithIgnoreCase(controllerName, "PADDLES"))
  {
    // Also check if we should swap the paddles plugged into a jack
    bool swapPaddles = myProperties.get(Controller_SwapPaddles) == "YES";
    bool swapAxis = false, swapDir = false;
    if(controllerName == "PADDLES_IAXIS")
      swapAxis = true;
    else if(controllerName == "PADDLES_IDIR")
      swapDir = true;
    else if(controllerName == "PADDLES_IAXDR")
      swapAxis = swapDir = true;
    controller = make_unique<Paddles>(port, myEvent, *mySystem,
                                      swapPaddles, swapAxis, swapDir);
  }
  else if(controllerName == "AMIGAMOUSE")
  {
    controller = make_unique<AmigaMouse>(port, myEvent, *mySystem);
  }
  else if(controllerName == "ATARIMOUSE")
  {
    controller = make_unique<AtariMouse>(port, myEvent, *mySystem);
  }
  else if(controllerName == "TRAKBALL")
  {
    controller = make_unique<TrakBall>(port, myEvent, *mySystem);
  }
  else if(controllerName == "ATARIVOX")
  {
    const string& nvramfile = myOSystem.nvramDir() + "atarivox_eeprom.dat";
    controller = make_unique<AtariVox>(port, myEvent,
        *mySystem, myOSystem.serialPort(),
        myOSystem.settings().getString("avoxport"), nvramfile);
  }
  else if(controllerName == "SAVEKEY")
  {
    const string& nvramfile = myOSystem.nvramDir() + "savekey_eeprom.dat";
    controller = make_unique<SaveKey>(port, myEvent, *mySystem,
                                      nvramfile);
  }
  else if(controllerName == "GENESIS")
  {
    controller = make_unique<Genesis>(port, myEvent, *mySystem);
  }
  else if(controllerName == "KIDVID")
  {
    controller = make_unique<KidVid>(port, myEvent, *mySystem, rommd5);
  }
  else if(controllerName == "MINDLINK")
  {
    controller = make_unique<MindLink>(port, myEvent, *mySystem);
  }
  else  // What else can we do?
    controller = make_unique<Joystick>(port, myEvent, *mySystem);

  return controller;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::loadUserPalette()
{
  const string& palette = myOSystem.paletteFile();
  ifstream in(palette, std::ios::binary);
  if(!in)
    return;

  // Make sure the contains enough data for the NTSC, PAL and SECAM palettes
  // This means 128 colours each for NTSC and PAL, at 3 bytes per pixel
  // and 8 colours for SECAM at 3 bytes per pixel
  in.seekg(0, std::ios::end);
  std::streampos length = in.tellg();
  in.seekg(0, std::ios::beg);
  if(length < 128 * 3 * 2 + 8 * 3)
  {
    cerr << "ERROR: invalid palette file " << palette << endl;
    return;
  }

  // Now that we have valid data, create the user-defined palettes
  uInt8 pixbuf[3];  // Temporary buffer for one 24-bit pixel

  for(int i = 0; i < 128; i++)  // NTSC palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    ourUserNTSCPalette[(i<<1)] = pixel;
  }
  for(int i = 0; i < 128; i++)  // PAL palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    ourUserPALPalette[(i<<1)] = pixel;
  }

  uInt32 secam[16];  // All 8 24-bit pixels, plus 8 colorloss pixels
  for(int i = 0; i < 8; i++)    // SECAM palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    secam[(i<<1)]   = pixel;
    secam[(i<<1)+1] = 0;
  }
  uInt32* ptr = ourUserSECAMPalette;
  for(int i = 0; i < 16; ++i)
  {
    uInt32* s = secam;
    for(int j = 0; j < 16; ++j)
      *ptr++ = *s++;
  }

  myUserPaletteDefined = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::generateColorLossPalette()
{
  // Look at all the palettes, since we don't know which one is
  // currently active
  uInt32* palette[9] = {
    &ourNTSCPalette[0],    &ourPALPalette[0],    &ourSECAMPalette[0],
    &ourNTSCPaletteZ26[0], &ourPALPaletteZ26[0], &ourSECAMPaletteZ26[0],
    nullptr, nullptr, nullptr
  };
  if(myUserPaletteDefined)
  {
    palette[6] = &ourUserNTSCPalette[0];
    palette[7] = &ourUserPALPalette[0];
    palette[8] = &ourUserSECAMPalette[0];
  }

  for(int i = 0; i < 9; ++i)
  {
    if(palette[i] == nullptr)
      continue;

    // Fill the odd numbered palette entries with gray values (calculated
    // using the standard RGB -> grayscale conversion formula)
    for(int j = 0; j < 128; ++j)
    {
      uInt32 pixel = palette[i][(j<<1)];
      uInt8 r = (pixel >> 16) & 0xff;
      uInt8 g = (pixel >> 8)  & 0xff;
      uInt8 b = (pixel >> 0)  & 0xff;
      uInt8 sum = uInt8((r * 0.2989) + (g * 0.5870) + (b * 0.1140));
      palette[i][(j<<1)+1] = (sum << 16) + (sum << 8) + sum;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
float Console::getFramerate() const
{
  return
    (myConsoleTiming == ConsoleTiming::ntsc ? 262.f * 60.f : 312.f * 50.f) /
     myTIA->frameBufferScanlinesLastFrame();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleTIABit(TIABit bit, const string& bitname, bool show) const
{
  bool result = myTIA->toggleBit(bit);
  string message = bitname + (result ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleBits() const
{
  bool enabled = myTIA->toggleBits();
  string message = string("TIA bits") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleTIACollision(TIABit bit, const string& bitname, bool show) const
{
  bool result = myTIA->toggleCollision(bit);
  string message = bitname + (result ? " collision enabled" : " collision disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleCollisions() const
{
  bool enabled = myTIA->toggleCollisions();
  string message = string("TIA collisions") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleFixedColors() const
{
  if(myTIA->toggleFixedColors())
    myOSystem.frameBuffer().showMessage("Fixed debug colors enabled");
  else
    myOSystem.frameBuffer().showMessage("Fixed debug colors disabled");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleJitter() const
{
  bool enabled = myTIA->toggleJitter();
  string message = string("TV scanline jitter") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::attachDebugger(Debugger& dbg)
{
#ifdef DEBUGGER_SUPPORT
//  myOSystem.createDebugger(*this);
  mySystem->m6502().attach(dbg);
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::stateChanged(EventHandlerState state)
{
  // For now, only the CompuMate cares about state changes
  if(myCMHandler)
    myCMHandler->enableKeyHandling(state == EventHandlerState::EMULATION);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourNTSCPalette[256] = {
  0x000000, 0, 0x4a4a4a, 0, 0x6f6f6f, 0, 0x8e8e8e, 0,
  0xaaaaaa, 0, 0xc0c0c0, 0, 0xd6d6d6, 0, 0xececec, 0,
  0x484800, 0, 0x69690f, 0, 0x86861d, 0, 0xa2a22a, 0,
  0xbbbb35, 0, 0xd2d240, 0, 0xe8e84a, 0, 0xfcfc54, 0,
  0x7c2c00, 0, 0x904811, 0, 0xa26221, 0, 0xb47a30, 0,
  0xc3903d, 0, 0xd2a44a, 0, 0xdfb755, 0, 0xecc860, 0,
  0x901c00, 0, 0xa33915, 0, 0xb55328, 0, 0xc66c3a, 0,
  0xd5824a, 0, 0xe39759, 0, 0xf0aa67, 0, 0xfcbc74, 0,
  0x940000, 0, 0xa71a1a, 0, 0xb83232, 0, 0xc84848, 0,
  0xd65c5c, 0, 0xe46f6f, 0, 0xf08080, 0, 0xfc9090, 0,
  0x840064, 0, 0x97197a, 0, 0xa8308f, 0, 0xb846a2, 0,
  0xc659b3, 0, 0xd46cc3, 0, 0xe07cd2, 0, 0xec8ce0, 0,
  0x500084, 0, 0x68199a, 0, 0x7d30ad, 0, 0x9246c0, 0,
  0xa459d0, 0, 0xb56ce0, 0, 0xc57cee, 0, 0xd48cfc, 0,
  0x140090, 0, 0x331aa3, 0, 0x4e32b5, 0, 0x6848c6, 0,
  0x7f5cd5, 0, 0x956fe3, 0, 0xa980f0, 0, 0xbc90fc, 0,
  0x000094, 0, 0x181aa7, 0, 0x2d32b8, 0, 0x4248c8, 0,
  0x545cd6, 0, 0x656fe4, 0, 0x7580f0, 0, 0x8490fc, 0,
  0x001c88, 0, 0x183b9d, 0, 0x2d57b0, 0, 0x4272c2, 0,
  0x548ad2, 0, 0x65a0e1, 0, 0x75b5ef, 0, 0x84c8fc, 0,
  0x003064, 0, 0x185080, 0, 0x2d6d98, 0, 0x4288b0, 0,
  0x54a0c5, 0, 0x65b7d9, 0, 0x75cceb, 0, 0x84e0fc, 0,
  0x004030, 0, 0x18624e, 0, 0x2d8169, 0, 0x429e82, 0,
  0x54b899, 0, 0x65d1ae, 0, 0x75e7c2, 0, 0x84fcd4, 0,
  0x004400, 0, 0x1a661a, 0, 0x328432, 0, 0x48a048, 0,
  0x5cba5c, 0, 0x6fd26f, 0, 0x80e880, 0, 0x90fc90, 0,
  0x143c00, 0, 0x355f18, 0, 0x527e2d, 0, 0x6e9c42, 0,
  0x87b754, 0, 0x9ed065, 0, 0xb4e775, 0, 0xc8fc84, 0,
  0x303800, 0, 0x505916, 0, 0x6d762b, 0, 0x88923e, 0,
  0xa0ab4f, 0, 0xb7c25f, 0, 0xccd86e, 0, 0xe0ec7c, 0,
  0x482c00, 0, 0x694d14, 0, 0x866a26, 0, 0xa28638, 0,
  0xbb9f47, 0, 0xd2b656, 0, 0xe8cc63, 0, 0xfce070, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourPALPalette[256] = {
  0x000000, 0, 0x121212, 0, 0x242424, 0, 0x484848, 0, // 180 0
  0x6c6c6c, 0, 0x909090, 0, 0xb4b4b4, 0, 0xd8d8d8, 0, // was 0x111111..0xcccccc
  0x000000, 0, 0x121212, 0, 0x242424, 0, 0x484848, 0, // 198 1
  0x6c6c6c, 0, 0x909090, 0, 0xb4b4b4, 0, 0xd8d8d8, 0,
  0x1d0f00, 0, 0x3f2700, 0, 0x614900, 0, 0x836b01, 0, // 1b0 2
  0xa58d23, 0, 0xc7af45, 0, 0xe9d167, 0, 0xffe789, 0, // was ..0xfff389
  0x002400, 0, 0x004600, 0, 0x216800, 0, 0x438a07, 0, // 1c8 3
  0x65ac29, 0, 0x87ce4b, 0, 0xa9f06d, 0, 0xcbff8f, 0,
  0x340000, 0, 0x561400, 0, 0x783602, 0, 0x9a5824, 0, // 1e0 4
  0xbc7a46, 0, 0xde9c68, 0, 0xffbe8a, 0, 0xffd0ad, 0, // was ..0xffe0ac
  0x002700, 0, 0x004900, 0, 0x0c6b0c, 0, 0x2e8d2e, 0, // 1f8 5
  0x50af50, 0, 0x72d172, 0, 0x94f394, 0, 0xb6ffb6, 0,
  0x3d0008, 0, 0x610511, 0, 0x832733, 0, 0xa54955, 0, // 210 6
  0xc76b77, 0, 0xe98d99, 0, 0xffafbb, 0, 0xffd1d7, 0, // was 0x3f0000..0xffd1dd
  0x001e12, 0, 0x004228, 0, 0x046540, 0, 0x268762, 0, // 228 7
  0x48a984, 0, 0x6acba6, 0, 0x8cedc8, 0, 0xafffe0, 0, // was 0x002100, 0x00431e..0xaeffff
  0x300025, 0, 0x5f0047, 0, 0x811e69, 0, 0xa3408b, 0, // 240 8
  0xc562ad, 0, 0xe784cf, 0, 0xffa8ea, 0, 0xffc9f2, 0, // was ..0xffa6f1, 0xffc8ff
  0x001431, 0, 0x003653, 0, 0x0a5875, 0, 0x2c7a97, 0, // 258 9
  0x4e9cb9, 0, 0x70bedb, 0, 0x92e0fd, 0, 0xb4ffff, 0,
  0x2c0052, 0, 0x4e0074, 0, 0x701d96, 0, 0x923fb8, 0, // 270 a
  0xb461da, 0, 0xd683fc, 0, 0xe2a5ff, 0, 0xeec9ff, 0, // was ..0xf8a5ff, 0xffc7ff
  0x001759, 0, 0x00247c, 0, 0x1d469e, 0, 0x3f68c0, 0, // 288 b
  0x618ae2, 0, 0x83acff, 0, 0xa5ceff, 0, 0xc7f0ff, 0,
  0x12006d, 0, 0x34038f, 0, 0x5625b1, 0, 0x7847d3, 0, // 2a0 c
  0x9a69f5, 0, 0xb48cff, 0, 0xc9adff, 0, 0xe1d1ff, 0, // was ..0xbc8bff, 0xdeadff, 0xffcfff,
  0x000070, 0, 0x161292, 0, 0x3834b4, 0, 0x5a56d6, 0, // 2b8 d
  0x7c78f8, 0, 0x9e9aff, 0, 0xc0bcff, 0, 0xe2deff, 0,
  0x000000, 0, 0x121212, 0, 0x242424, 0, 0x484848, 0, // 2d0 e
  0x6c6c6c, 0, 0x909090, 0, 0xb4b4b4, 0, 0xd8d8d8, 0,
  0x000000, 0, 0x121212, 0, 0x242424, 0, 0x484848, 0, // 2e8 f
  0x6c6c6c, 0, 0x909090, 0, 0xb4b4b4, 0, 0xd8d8d8, 0,
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourSECAMPalette[256] = {
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourNTSCPaletteZ26[256] = {
  0x000000, 0, 0x505050, 0, 0x646464, 0, 0x787878, 0,
  0x8c8c8c, 0, 0xa0a0a0, 0, 0xb4b4b4, 0, 0xc8c8c8, 0,
  0x445400, 0, 0x586800, 0, 0x6c7c00, 0, 0x809000, 0,
  0x94a414, 0, 0xa8b828, 0, 0xbccc3c, 0, 0xd0e050, 0,
  0x673900, 0, 0x7b4d00, 0, 0x8f6100, 0, 0xa37513, 0,
  0xb78927, 0, 0xcb9d3b, 0, 0xdfb14f, 0, 0xf3c563, 0,
  0x7b2504, 0, 0x8f3918, 0, 0xa34d2c, 0, 0xb76140, 0,
  0xcb7554, 0, 0xdf8968, 0, 0xf39d7c, 0, 0xffb190, 0,
  0x7d122c, 0, 0x912640, 0, 0xa53a54, 0, 0xb94e68, 0,
  0xcd627c, 0, 0xe17690, 0, 0xf58aa4, 0, 0xff9eb8, 0,
  0x730871, 0, 0x871c85, 0, 0x9b3099, 0, 0xaf44ad, 0,
  0xc358c1, 0, 0xd76cd5, 0, 0xeb80e9, 0, 0xff94fd, 0,
  0x5d0b92, 0, 0x711fa6, 0, 0x8533ba, 0, 0x9947ce, 0,
  0xad5be2, 0, 0xc16ff6, 0, 0xd583ff, 0, 0xe997ff, 0,
  0x401599, 0, 0x5429ad, 0, 0x683dc1, 0, 0x7c51d5, 0,
  0x9065e9, 0, 0xa479fd, 0, 0xb88dff, 0, 0xcca1ff, 0,
  0x252593, 0, 0x3939a7, 0, 0x4d4dbb, 0, 0x6161cf, 0,
  0x7575e3, 0, 0x8989f7, 0, 0x9d9dff, 0, 0xb1b1ff, 0,
  0x0f3480, 0, 0x234894, 0, 0x375ca8, 0, 0x4b70bc, 0,
  0x5f84d0, 0, 0x7398e4, 0, 0x87acf8, 0, 0x9bc0ff, 0,
  0x04425a, 0, 0x18566e, 0, 0x2c6a82, 0, 0x407e96, 0,
  0x5492aa, 0, 0x68a6be, 0, 0x7cbad2, 0, 0x90cee6, 0,
  0x044f30, 0, 0x186344, 0, 0x2c7758, 0, 0x408b6c, 0,
  0x549f80, 0, 0x68b394, 0, 0x7cc7a8, 0, 0x90dbbc, 0,
  0x0f550a, 0, 0x23691e, 0, 0x377d32, 0, 0x4b9146, 0,
  0x5fa55a, 0, 0x73b96e, 0, 0x87cd82, 0, 0x9be196, 0,
  0x1f5100, 0, 0x336505, 0, 0x477919, 0, 0x5b8d2d, 0,
  0x6fa141, 0, 0x83b555, 0, 0x97c969, 0, 0xabdd7d, 0,
  0x344600, 0, 0x485a00, 0, 0x5c6e14, 0, 0x708228, 0,
  0x84963c, 0, 0x98aa50, 0, 0xacbe64, 0, 0xc0d278, 0,
  0x463e00, 0, 0x5a5205, 0, 0x6e6619, 0, 0x827a2d, 0,
  0x968e41, 0, 0xaaa255, 0, 0xbeb669, 0, 0xd2ca7d, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourPALPaletteZ26[256] = {
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x533a00, 0, 0x674e00, 0, 0x7b6203, 0, 0x8f7617, 0,
  0xa38a2b, 0, 0xb79e3f, 0, 0xcbb253, 0, 0xdfc667, 0,
  0x1b5800, 0, 0x2f6c00, 0, 0x438001, 0, 0x579415, 0,
  0x6ba829, 0, 0x7fbc3d, 0, 0x93d051, 0, 0xa7e465, 0,
  0x6a2900, 0, 0x7e3d12, 0, 0x925126, 0, 0xa6653a, 0,
  0xba794e, 0, 0xce8d62, 0, 0xe2a176, 0, 0xf6b58a, 0,
  0x075b00, 0, 0x1b6f11, 0, 0x2f8325, 0, 0x439739, 0,
  0x57ab4d, 0, 0x6bbf61, 0, 0x7fd375, 0, 0x93e789, 0,
  0x741b2f, 0, 0x882f43, 0, 0x9c4357, 0, 0xb0576b, 0,
  0xc46b7f, 0, 0xd87f93, 0, 0xec93a7, 0, 0xffa7bb, 0,
  0x00572e, 0, 0x106b42, 0, 0x247f56, 0, 0x38936a, 0,
  0x4ca77e, 0, 0x60bb92, 0, 0x74cfa6, 0, 0x88e3ba, 0,
  0x6d165f, 0, 0x812a73, 0, 0x953e87, 0, 0xa9529b, 0,
  0xbd66af, 0, 0xd17ac3, 0, 0xe58ed7, 0, 0xf9a2eb, 0,
  0x014c5e, 0, 0x156072, 0, 0x297486, 0, 0x3d889a, 0,
  0x519cae, 0, 0x65b0c2, 0, 0x79c4d6, 0, 0x8dd8ea, 0,
  0x5f1588, 0, 0x73299c, 0, 0x873db0, 0, 0x9b51c4, 0,
  0xaf65d8, 0, 0xc379ec, 0, 0xd78dff, 0, 0xeba1ff, 0,
  0x123b87, 0, 0x264f9b, 0, 0x3a63af, 0, 0x4e77c3, 0,
  0x628bd7, 0, 0x769feb, 0, 0x8ab3ff, 0, 0x9ec7ff, 0,
  0x451e9d, 0, 0x5932b1, 0, 0x6d46c5, 0, 0x815ad9, 0,
  0x956eed, 0, 0xa982ff, 0, 0xbd96ff, 0, 0xd1aaff, 0,
  0x2a2b9e, 0, 0x3e3fb2, 0, 0x5253c6, 0, 0x6667da, 0,
  0x7a7bee, 0, 0x8e8fff, 0, 0xa2a3ff, 0, 0xb6b7ff, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourSECAMPaletteZ26[256] = {
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserNTSCPalette[256]  = { 0 }; // filled from external file

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserPALPalette[256]   = { 0 }; // filled from external file

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserSECAMPalette[256] = { 0 }; // filled from external file
