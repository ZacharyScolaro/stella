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

#include "CartF4.hxx"
#include "PopUpWidget.hxx"
#include "CartF4Widget.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CartridgeF4Widget::CartridgeF4Widget(
      GuiObject* boss, const GUI::Font& lfont, const GUI::Font& nfont,
      int x, int y, int w, int h, CartridgeF4& cart)
  : CartDebugWidget(boss, lfont, nfont, x, y, w, h),
    myCart(cart)
{
  uInt16 size = 8 * 4096;

  ostringstream info;
  info << "Standard F4 cartridge, eight 4K banks\n"
       << "Startup bank = " << cart.startBank() << " or undetermined\n";

  // Eventually, we should query this from the debugger/disassembler
  for(uInt32 i = 0, offset = 0xFFC, spot = 0xFF4; i < 8; ++i, offset += 0x1000)
  {
    uInt16 start = (cart.myImage[offset+1] << 8) | cart.myImage[offset];
    start -= start % 0x1000;
    info << "Bank " << i << " @ $" << Common::Base::HEX4 << start << " - "
         << "$" << (start + 0xFFF) << " (hotspot = $F" << (spot+i) << ")\n";
  }

  int xpos = 10,
      ypos = addBaseInformation(size, "Atari", info.str(), 15) + myLineHeight;

  VariantList items;
  VarList::push_back(items, "0 ($FFF4)");
  VarList::push_back(items, "1 ($FFF5)");
  VarList::push_back(items, "2 ($FFF6)");
  VarList::push_back(items, "3 ($FFF7)");
  VarList::push_back(items, "4 ($FFF8)");
  VarList::push_back(items, "5 ($FFF9)");
  VarList::push_back(items, "6 ($FFFA)");
  VarList::push_back(items, "7 ($FFFB)");
  myBank =
    new PopUpWidget(boss, _font, xpos, ypos-2, _font.getStringWidth("0 ($FFFx) "),
                    myLineHeight, items, "Set bank ",
                    _font.getStringWidth("Set bank "), kBankChanged);
  myBank->setTarget(this);
  addFocusWidget(myBank);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeF4Widget::loadConfig()
{
  Debugger& dbg = instance().debugger();
  CartDebug& cart = dbg.cartDebug();
  const CartState& state = static_cast<const CartState&>(cart.getState());
  const CartState& oldstate = static_cast<const CartState&>(cart.getOldState());

  myBank->setSelectedIndex(myCart.getBank(), state.bank != oldstate.bank);

  CartDebugWidget::loadConfig();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartridgeF4Widget::handleCommand(CommandSender* sender,
                                      int cmd, int data, int id)
{
  if(cmd == kBankChanged)
  {
    myCart.unlockBank();
    myCart.bank(myBank->getSelected());
    myCart.lockBank();
    invalidate();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
string CartridgeF4Widget::bankState()
{
  ostringstream& buf = buffer();

  static const char* const spot[] = {
    "$FFF4", "$FFF5", "$FFF6", "$FFF7", "$FFF8", "$FFF9", "$FFFA", "$FFFB"
  };
  buf << "Bank = " << std::dec << myCart.getBank()
      << ", hotspot = " << spot[myCart.getBank()];

  return buf.str();
}
