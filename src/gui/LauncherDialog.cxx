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

#include "bspf.hxx"
#include "Bankswitch.hxx"
#include "BrowserDialog.hxx"
#include "ContextMenu.hxx"
#include "DialogContainer.hxx"
#include "Dialog.hxx"
#include "EditTextWidget.hxx"
#include "FSNode.hxx"
#include "GameList.hxx"
#include "MD5.hxx"
#include "OptionsDialog.hxx"
#include "GlobalPropsDialog.hxx"
#include "MessageBox.hxx"
#include "OSystem.hxx"
#include "FrameBuffer.hxx"
#include "EventHandler.hxx"
#include "StellaKeys.hxx"
#include "Props.hxx"
#include "PropsSet.hxx"
#include "RomInfoWidget.hxx"
#include "Settings.hxx"
#include "StringListWidget.hxx"
#include "Widget.hxx"
#include "Font.hxx"
#include "LauncherDialog.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
LauncherDialog::LauncherDialog(OSystem& osystem, DialogContainer& parent,
                               int x, int y, int w, int h)
  : Dialog(osystem, parent, x, y, w, h),
    myStartButton(nullptr),
    myPrevDirButton(nullptr),
    myOptionsButton(nullptr),
    myQuitButton(nullptr),
    myList(nullptr),
    myPattern(nullptr),
    myRomInfoWidget(nullptr),
    mySelectedItem(0)
{
  const GUI::Font& font = instance().frameBuffer().launcherFont();

  const int HBORDER = 10;
  const int BUTTON_GAP = 8;
  const int fontWidth = font.getMaxCharWidth(),
            fontHeight = font.getFontHeight(),
            lineHeight = font.getLineHeight(),
            bwidth  = (_w - 2 * HBORDER - BUTTON_GAP * (4 - 1)),
            bheight = lineHeight + 4;
  int xpos, ypos = 0, lwidth = 0, lwidth2 = 0;
  WidgetArray wid;

  // Show game name
  lwidth = font.getStringWidth("Select a ROM from the list" + ELLIPSIS);
  xpos = HBORDER;  ypos += 8;
  new StaticTextWidget(this, font, xpos, ypos, lwidth, fontHeight,
                       "Select a ROM from the list" + ELLIPSIS);

  lwidth2 = font.getStringWidth("XXXX items found");
  xpos = _w - lwidth2 - 10;
  myRomCount = new StaticTextWidget(this, font, xpos, ypos,
                                    lwidth2, fontHeight,
                                    "", TextAlign::Right);

  // Add filter that can narrow the results shown in the listing
  // It has to fit between both labels
  if(w >= 640)
  {
    int fwidth = std::min(15 * fontWidth, xpos - 20 - lwidth);

    new StaticTextWidget(this, font, xpos - fwidth - 5 - font.getStringWidth("Filter "),
                         ypos, "Filter ");
    xpos -= fwidth + 5;
    myPattern = new EditTextWidget(this, font, xpos, ypos - 2,
                                   fwidth, lineHeight, "");
  }

  // Add list with game titles
  // Before we add the list, we need to know the size of the RomInfoWidget
  xpos = 10;  ypos += lineHeight + 4;
  int romWidth = 0;
  int romSize = instance().settings().getInt("romviewer");
  if(romSize > 1 && w >= 1000 && h >= 760)
    romWidth = 660;
  else if(romSize > 0 && w >= 640 && h >= 480)
    romWidth = 365;

  int listWidth = _w - (romWidth > 0 ? romWidth+8 : 0) - 20;
  myList = new StringListWidget(this, font, xpos, ypos,
                                listWidth, _h - 43 - bheight - fontHeight - lineHeight);
  myList->setEditable(false);
  wid.push_back(myList);
  if(myPattern)  wid.push_back(myPattern);  // Add after the list for tab order

  // Add ROM info area (if enabled)
  if(romWidth > 0)
  {
    xpos += myList->getWidth() + 8;
    myRomInfoWidget = new RomInfoWidget(this,
        romWidth < 660 ? instance().frameBuffer().smallFont() :
                         instance().frameBuffer().infoFont(),
        xpos, ypos, romWidth, myList->getHeight());
  }

  // Add textfield to show current directory
  xpos = 10;
  ypos += myList->getHeight() + 8;
  lwidth = font.getStringWidth("Path ");
  myDirLabel = new StaticTextWidget(this, font, xpos, ypos+2, lwidth, fontHeight,
                                    "Path", TextAlign::Left);
  xpos += lwidth;
  myDir = new EditTextWidget(this, font, xpos, ypos, _w - xpos - 10, lineHeight, "");
  myDir->setEditable(false, true);
  myDir->clearFlags(WIDGET_RETAIN_FOCUS);

  // Add four buttons at the bottom
  xpos = 10;  ypos += myDir->getHeight() + 8;
#ifndef BSPF_MAC_OSX
  myStartButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 0) / 4, bheight,
                                  "Select", kLoadROMCmd);
  wid.push_back(myStartButton);
    xpos += (bwidth + 0) / 4 + BUTTON_GAP;
  myPrevDirButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 1) / 4, bheight,
                                     "Go Up", kPrevDirCmd);
  wid.push_back(myPrevDirButton);
    xpos += (bwidth + 1) / 4 + BUTTON_GAP;
    myOptionsButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 2) / 4, bheight,
                                       "Options" + ELLIPSIS, kOptionsCmd);
  wid.push_back(myOptionsButton);
    xpos += (bwidth + 2) / 4 + BUTTON_GAP;
  myQuitButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 3) / 4, bheight,
                                  "Quit", kQuitCmd);
  wid.push_back(myQuitButton);
#else
  myQuitButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 0) / 4, bheight,
                                  "Quit", kQuitCmd);
  wid.push_back(myQuitButton);
    xpos += (bwidth + 0) / 4 + BUTTON_GAP;
  myOptionsButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 1) / 4, bheight,
                                     "Options" + ELLIPSIS, kOptionsCmd);
  wid.push_back(myOptionsButton);
    xpos += (bwidth + 1) / 4 + BUTTON_GAP;
  myPrevDirButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 2) / 4, bheight,
                                      "Go Up", kPrevDirCmd);
  wid.push_back(myPrevDirButton);
    xpos += (bwidth + 2) / 4 + BUTTON_GAP;
  myStartButton = new ButtonWidget(this, font, xpos, ypos, (bwidth + 3) / 4, bheight,
                                   "Select", kLoadROMCmd);
  wid.push_back(myStartButton);
#endif
  mySelectedItem = 0;  // Highlight 'Rom Listing'

  // Create an options dialog, similar to the in-game one
  myOptions = make_unique<OptionsDialog>(osystem, parent, this, w, h, OptionsDialog::launcher);

  // Create a game list, which contains all the information about a ROM that
  // the launcher needs
  myGameList = make_unique<GameList>();

  addToFocusList(wid);

  // Create context menu for ROM list options
  VariantList l;
  VarList::push_back(l, "Power-on options" + ELLIPSIS, "override");
  VarList::push_back(l, "Show only ROM files", "roms");
  VarList::push_back(l, "Show all files", "allfiles");
  VarList::push_back(l, "Reload listing", "reload");
  myMenu = make_unique<ContextMenu>(this, osystem.frameBuffer().font(), l);

  // Create global props dialog, which is used to temporarily overrride
  // ROM properties
  myGlobalProps = make_unique<GlobalPropsDialog>(this, osystem.frameBuffer().font());

  // Do we show only ROMs or all files?
  showOnlyROMs(instance().settings().getBool("launcherroms"));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const string& LauncherDialog::selectedRomMD5()
{
  int item = myList->getSelected();
  if(item < 0)
    return EmptyString;

  const FilesystemNode node(myGameList->path(item));
  if(node.isDirectory() || !Bankswitch::isValidRomName(node))
    return EmptyString;

  // Make sure we have a valid md5 for this ROM
  if(myGameList->md5(item) == "")
    myGameList->setMd5(item, MD5::hash(node));

  return myGameList->md5(item);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::loadConfig()
{
  // Should we use a temporary directory specified on the commandline, or the
  // default one specified by the settings?
  const string& tmpromdir = instance().settings().getString("tmpromdir");
  const string& romdir = tmpromdir != "" ? tmpromdir :
      instance().settings().getString("romdir");

  // Assume that if the list is empty, this is the first time that loadConfig()
  // has been called (and we should reload the list)
  if(myList->getList().empty())
  {
    myPrevDirButton->setEnabled(false);
    myCurrentNode = FilesystemNode(romdir == "" ? "~" : romdir);
    if(!(myCurrentNode.exists() && myCurrentNode.isDirectory()))
      myCurrentNode = FilesystemNode("~");

    updateListing();
  }
  Dialog::setFocus(getFocusList()[mySelectedItem]);

  if(myRomInfoWidget)
    myRomInfoWidget->loadConfig();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::updateListing(const string& nameToSelect)
{
  // Start with empty list
  myGameList->clear();
  myDir->setText("");

  loadDirListing();

  // Only hilite the 'up' button if there's a parent directory
  myPrevDirButton->setEnabled(myCurrentNode.hasParent());

  // Show current directory
  myDir->setText(myCurrentNode.getShortPath());

  // Now fill the list widget with the contents of the GameList
  StringList l;
  for(uInt32 i = 0; i < myGameList->size(); ++i)
    l.push_back(myGameList->name(i));

  myList->setList(l);

  // Indicate how many files were found
  ostringstream buf;
  buf << (myGameList->size() - 1) << " items found";
  myRomCount->setLabel(buf.str());

  // Restore last selection
  const string& find =
    nameToSelect == "" ? instance().settings().getString("lastrom") : nameToSelect;
  myList->setSelected(find);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::loadDirListing()
{
  if(!myCurrentNode.isDirectory())
    return;

  FSList files;
  files.reserve(2048);
  myCurrentNode.getChildren(files, FilesystemNode::kListAll);

  // Add '[..]' to indicate previous folder
  if(myCurrentNode.hasParent())
    myGameList->appendGame(" [..]", "", "", true);

  // Now add the directory entries
  bool domatch = myPattern && myPattern->getText() != "";
  for(const auto& f: files)
  {
    bool isDir = f.isDirectory();
    const string& name = isDir ? (" [" + f.getName() + "]") : f.getName();

    // Do we want to show only ROMs or all files?
    if(!isDir && myShowOnlyROMs && !Bankswitch::isValidRomName(f))
      continue;

    // Skip over files that don't match the pattern in the 'pattern' textbox
    if(domatch && !isDir && !matchPattern(name, myPattern->getText()))
      continue;

    myGameList->appendGame(name, f.getPath(), "", isDir);
  }

  // Sort the list by rom name (since that's what we see in the listview)
  myGameList->sortByName();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::loadRomInfo()
{
  if(!myRomInfoWidget) return;
  int item = myList->getSelected();
  if(item < 0) return;

  const FilesystemNode node(myGameList->path(item));
  if(!node.isDirectory() && Bankswitch::isValidRomName(node))
  {
    // Make sure we have a valid md5 for this ROM
    if(myGameList->md5(item) == "")
      myGameList->setMd5(item, MD5::hash(node));

    // Get the properties for this entry
    Properties props;
    instance().propSet().getMD5WithInsert(node, myGameList->md5(item), props);

    myRomInfoWidget->setProperties(props);
  }
  else
    myRomInfoWidget->clearProperties();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::handleContextMenu()
{
  const string& cmd = myMenu->getSelectedTag().toString();

  if(cmd == "override")
  {
    myGlobalProps->open();
  }
  else if(cmd == "roms" || cmd == "allfiles")
  {
    showOnlyROMs(cmd == "roms");
    updateListing();
  }
  else if(cmd == "reload")
  {
    updateListing();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::showOnlyROMs(bool state)
{
  myShowOnlyROMs = state;
  instance().settings().setValue("launcherroms", state);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool LauncherDialog::matchPattern(const string& s, const string& pattern) const
{
  // This method is modelled after strcasestr, which we don't use
  // because it isn't guaranteed to be available everywhere
  // The strcasestr uses the KMP algorithm when the comparisons
  // reach a certain point, but since we'll be dealing with relatively
  // short strings, I think the overhead of building a KMP table
  // each time would be slower than the brute force method used here
  const char* haystack = s.c_str();
  const char* needle = pattern.c_str();

  uInt8 b = tolower(*needle);

  needle++;
  for(;; haystack++)
  {
    if(*haystack == '\0')  /* No match */
      return false;

    /* The first character matches */
    if(tolower(*haystack) == b)
    {
      const char* rhaystack = haystack + 1;
      const char* rneedle = needle;

      for(;; rhaystack++, rneedle++)
      {
        if(*rneedle == '\0')   /* Found a match */
          return true;
        if(*rhaystack == '\0') /* No match */
          return false;

        /* Nothing in this round */
        if(tolower(*rhaystack) != tolower(*rneedle))
          break;
      }
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::handleKeyDown(StellaKey key, StellaMod mod)
{
  // Grab the key before passing it to the actual dialog and check for
  // Control-R (reload ROM listing)
  if(StellaModTest::isControl(mod) && key == KBDK_R)
    updateListing();
  else
    Dialog::handleKeyDown(key, mod);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::handleMouseDown(int x, int y, MouseButton b, int clickCount)
{
  // Grab right mouse button for context menu, send left to base class
  if(b == MouseButton::RIGHT)
  {
    // Add menu at current x,y mouse location
    myMenu->show(x + getAbsX(), y + getAbsY());
  }
  else
    Dialog::handleMouseDown(x, y, b, clickCount);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void LauncherDialog::handleCommand(CommandSender* sender, int cmd,
                                   int data, int id)
{
  switch (cmd)
  {
    case kLoadROMCmd:
    case ListWidget::kActivatedCmd:
    case ListWidget::kDoubleClickedCmd:
    {
      int item = myList->getSelected();
      if(item >= 0)
      {
        const FilesystemNode romnode(myGameList->path(item));

        // Directory's should be selected (ie, enter them and redisplay)
        if(romnode.isDirectory())
        {
          string dirname = "";
          if(myGameList->name(item) == " [..]")
          {
            myCurrentNode = myCurrentNode.getParent();
            if(!myNodeNames.empty())
              dirname = myNodeNames.pop();
          }
          else
          {
            myCurrentNode = romnode;
            myNodeNames.push(myGameList->name(item));
          }
          updateListing(dirname);
        }
        else
        {
          const string& result =
            instance().createConsole(romnode, myGameList->md5(item));
          if(result == EmptyString)
          {
            instance().settings().setValue("lastrom", myList->getSelectedString());

            // If romdir has never been set, set it now based on the selected rom
            if(instance().settings().getString("romdir") == EmptyString)
              instance().settings().setValue("romdir", romnode.getParent().getShortPath());
          }
          else
            instance().frameBuffer().showMessage(result, MessagePosition::MiddleCenter, true);
        }
      }
      break;
    }

    case kOptionsCmd:
      myOptions->open();
      break;

    case kPrevDirCmd:
    case ListWidget::kPrevDirCmd:
      myCurrentNode = myCurrentNode.getParent();
      updateListing(myNodeNames.empty() ? "" : myNodeNames.pop());
      break;

    case ListWidget::kSelectionChangedCmd:
      loadRomInfo();
      break;

    case kQuitCmd:
      close();
      instance().eventHandler().quit();
      break;

    case kRomDirChosenCmd:
      myCurrentNode = FilesystemNode(instance().settings().getString("romdir"));
      if(!(myCurrentNode.exists() && myCurrentNode.isDirectory()))
        myCurrentNode = FilesystemNode("~");
      updateListing();
      break;

    case kReloadRomDirCmd:
      updateListing();
      break;

    case kOnlyROMsCmd:
      showOnlyROMs(data);  // NOTE: present for when we add a widget for this
      updateListing();
      break;

    case ContextMenu::kItemSelectedCmd:
      handleContextMenu();
      break;

    case EditableWidget::kAcceptCmd:
    case EditableWidget::kChangedCmd:
      // The updateListing() method knows what to do when the text changes
      updateListing();
      break;

    default:
      Dialog::handleCommand(sender, cmd, data, 0);
  }
}
