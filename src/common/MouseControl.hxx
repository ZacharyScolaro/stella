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

#ifndef MOUSE_CONTROL_HXX
#define MOUSE_CONTROL_HXX

class Console;
class Controller;
class Properties;

#include "bspf.hxx"

/**
  The mouse can control various virtual 'controllers' in many different
  ways.  In 'auto' mode, the entire mouse (both axes and buttons) are used
  as one controller.  In per-ROM axis mode, each axis/button may control
  separate controllers.  As well, we'd like to switch dynamically between
  each of these modes at runtime.

  This class encapsulates all required info to implement this functionality.

  @author  Stephen Anthony
*/
class MouseControl
{
  public:
    /**
      Enumeration of mouse axis control types
    */
    enum Axis
    {
      Paddle0 = 0, Paddle1, Paddle2, Paddle3,
      Driving0, Driving1, MindLink0, MindLink1,
      NoControl
    };

  public:
    /**
      Create a new MouseControl object

      @param console The console in use by the system
      @param mode    Contains information about how to use the mouse axes/buttons
    */
    MouseControl(Console& console, const string& mode);

    /**
      Cycle through each available mouse control mode

      @return  A message explaining the current mouse mode
    */
    const string& next();

  private:
    void addLeftControllerModes(bool noswap);
    void addRightControllerModes(bool noswap);
    void addPaddleModes(int lport, int rport, int lname, int rname);
    bool controllerSupportsMouse(Controller& controller);

  private:
    const Properties& myProps;
    Controller& myLeftController;
    Controller& myRightController;

    struct MouseMode {
      Controller::Type xtype, ytype;
      int xid, yid;
      string message;

      explicit MouseMode(const string& msg = "")
        : xtype(Controller::Joystick),
          ytype(Controller::Joystick),
          xid(-1),
          yid(-1),
          message(msg)  { }
      MouseMode(Controller::Type xt, int xi,
                Controller::Type yt, int yi,
                const string& msg)
        : xtype(xt),
          ytype(yt),
          xid(xi),
          yid(yi),
          message(msg)  { }

      friend ostream& operator<<(ostream& os, const MouseMode& mm)
      {
        os << "xtype=" << mm.xtype << ", xid=" << mm.xid
           << ", ytype=" << mm.ytype << ", yid=" << mm.yid
           << ", msg=" << mm.message;
        return os;
      }
    };

    int myCurrentModeNum;
    vector<MouseMode> myModeList;

  private:
    // Following constructors and assignment operators not supported
    MouseControl() = delete;
    MouseControl(const MouseControl&) = delete;
    MouseControl(MouseControl&&) = delete;
    MouseControl& operator=(const MouseControl&) = delete;
    MouseControl& operator=(MouseControl&&) = delete;
};

#endif
