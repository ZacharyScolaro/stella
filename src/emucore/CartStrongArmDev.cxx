#include "System.hxx"
#include "M6532.hxx"
#include "TIA.hxx"
#include "CartStrongArmDev.hxx"
#include <atomic>
#include "games/GorillaForce/gorillaForce.h"
#include "games/RayCasterDemo/RayCasterDemo.h"
#include "games/WushuMasters/WushuMasters.h"
#include "vcsLib.h"

#include <thread>
#include <mutex>
#include <condition_variable>

static std::mutex m;
static std::condition_variable cv;
static uInt8 lastReadValue = 0xff;
static uInt16 nextRomIndex;
static uInt16 nextStuffIndex;
static uInt16 nextJumpTarget;
static CartStrongArmDev * _cart;
static bool _runGame;
static bool _runEmulator;

static void startGame()
{
	vcsJmp3();
	//rayCasterDemo();
	wushuMasters();
}


uInt16 RunStrongArmGame()
{
	nextStuffIndex = 0;
	nextRomIndex = nextJumpTarget & 0xfff;
	// Wait until stella has executed the JMP
	std::unique_lock<std::mutex> lk(m);
	_runEmulator = false;
	_runGame = true;
	cv.notify_one();
	cv.wait(lk, [] {return _runEmulator; });
	lk.unlock();
	return nextJumpTarget;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
CartStrongArmDev::CartStrongArmDev(const BytePtr& image, uInt32 size,
                         const Settings& settings)
  : Cartridge(settings)
{
	_cart = this;

	// Start filling in ROM at 0x1000
	nextRomIndex = 0;
	nextStuffIndex = 0;
	// Set Reset vector to 0x1000
	_cart->_romHistory[0xffc] = (0x00);
	_cart->_romHistory[0xffd] = (0x10);

	for (int i = 0; i < 4096; i++)
	{
		_stuffHistory[i] = 0;
	}
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartStrongArmDev::reset()
{
	_gameThread = new std::thread(startGame);
	nextStuffIndex = 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void CartStrongArmDev::install(System& system)
{
  mySystem = &system;

  System::PageAccess access(this, System::PA_READ);

  // Strong ARM owns the entire address space
  for(uInt16 addr = 0x000; addr < 0x2000; addr += System::PAGE_SIZE)
    mySystem->setPageAccess(addr, access);
}

static uInt16 lastPeekAddress;
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 CartStrongArmDev::peek(uInt16 address)
{
	lastPeekAddress = address &= 0x1FFF;

  uInt8 value = 0;

  if ((address & 0x1080) == 0)
  {
	  value = mySystem->tia().peek(address);
  }
  else if ((address & 0x1080) == 0x0080)
  {
	  value = mySystem->m6532().peek(address);
  }
  else
  {
	  value = _romHistory[address & 0x0fff];
  }
  
  lastReadValue = value;
  return value;
}

static int stuffCount;
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartStrongArmDev::poke(uInt16 address, uInt8 value)
{
  address &= 0x1FFF;

  if ((nextStuffIndex < stuffCount) && ((_stuffHistory[nextStuffIndex] >> 16) == lastPeekAddress))
  {
	  value = _stuffHistory[nextStuffIndex] & 0xff;
	  nextStuffIndex++;

  }

  if ((address & 0x1080) == 0x0)
  {
	  mySystem->tia().poke(address, value);
  }
  else   if ((address & 0x1080) == 0x0080)
  {
	  mySystem->m6532().poke(address, value);
  }

  return false;
}


bool CartStrongArmDev::patch(uInt16 address, uInt8 value)
{
	if ((address & 0x1000) == 0x1000)
	{
		_romHistory[address & 0x0fff] = value;
		return true;
	}
	return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const uInt8* CartStrongArmDev::getImage(uInt32& size) const
{
  size = 4096;
  return _romHistory;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartStrongArmDev::save(Serializer& out) const
{
  try
  {
  }
  catch(...)
  {
    cerr << "ERROR: CartStrongArmDev::save" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool CartStrongArmDev::load(Serializer& in)
{
  try
  {
  }
  catch(...)
  {
    cerr << "ERROR: CartStrongArmDev::load" << endl;
    return false;
  }

  return true;
}

// Strong ARM dev implementation
unsigned char vcsRead4(unsigned short address)
{
	_cart->_romHistory[nextRomIndex++] = (0xad);
	_cart->_romHistory[nextRomIndex++] = (address & 0xff);
	_cart->_romHistory[nextRomIndex++] = (address >> 8);
	stuffCount = nextStuffIndex;
	nextStuffIndex = 0;
	// Wait until stella has executed the Read
	nextJumpTarget = 0x1000 | nextRomIndex;
	std::unique_lock<std::mutex> lk(m);
	_runGame = false;
	_runEmulator = true;
	cv.notify_one();
	cv.wait(lk, [] {return _runGame; });
	lk.unlock();
	return lastReadValue;
}


void vcsJmp3()
{
	_cart->_romHistory[nextRomIndex++] = (0x4c);
	_cart->_romHistory[nextRomIndex++] = (0x00);
	_cart->_romHistory[nextRomIndex++] = (0x10);
	nextRomIndex = 0;
	nextJumpTarget = 0x1000;
	stuffCount = nextStuffIndex;
	nextStuffIndex = 0;
	// Wait until stella has executed the JMP
	std::unique_lock<std::mutex> lk(m);
	_runGame = false;
	_runEmulator = true;
	cv.notify_one();
	cv.wait(lk, [] {return _runGame; });
	lk.unlock();
}

void StartOverblank()
{
	_cart->_romHistory[0xfff] = (0xff);
	_cart->_romHistory[nextRomIndex++] = (0x4c);
	_cart->_romHistory[nextRomIndex++] = (0x80);
	_cart->_romHistory[nextRomIndex++] = (0x00);
}

void EndOverblank()
{
	_cart->_romHistory[0xfff] = (0x00);
	nextRomIndex = 0;
	nextJumpTarget = 0x1000;
	stuffCount = nextStuffIndex;
	nextStuffIndex = 0;
	// Wait until stella has executed the JMP
	std::unique_lock<std::mutex> lk(m);
	_runGame = false;
	_runEmulator = true;
	cv.notify_one();
	cv.wait(lk, [] {return _runGame; });
	lk.unlock();
}

void vcsWrite3(unsigned char ZP, unsigned char data)
{
	_cart->_romHistory[nextRomIndex++] = (0x85);
	_cart->_stuffHistory[nextStuffIndex++] = ((uInt16)(nextRomIndex | 0x1000) << 16) | (data);
	_cart->_romHistory[nextRomIndex++] = (ZP);
}

void vcsWrite5(unsigned char ZP, unsigned char data)
{
	_cart->_romHistory[nextRomIndex++] = (0xa9);
	_cart->_romHistory[nextRomIndex++] = (data);
	_cart->_romHistory[nextRomIndex++] = (0x85);
	_cart->_romHistory[nextRomIndex++] = (ZP);
}

void vcsLda2(unsigned char data)
{
	_cart->_romHistory[nextRomIndex++] = (0xa9);
	_cart->_romHistory[nextRomIndex++] = (data);
}

void vcsLdx2(unsigned char data)
{
	_cart->_romHistory[nextRomIndex++] = (0xa2);
	_cart->_romHistory[nextRomIndex++] = (data);
}

void vcsLdy2(unsigned char data)
{
	_cart->_romHistory[nextRomIndex++] = (0xa0);
	_cart->_romHistory[nextRomIndex++] = (data);
}

void vcsSta3(unsigned char ZP)
{
	_cart->_romHistory[nextRomIndex++] = (0x85);
	_cart->_romHistory[nextRomIndex++] = (ZP);
}

void vcsSta4(unsigned char ZP)
{
	_cart->_romHistory[nextRomIndex++] = (0x8d);
	_cart->_romHistory[nextRomIndex++] = (ZP);
	_cart->_romHistory[nextRomIndex++] = (00);
}

void vcsStx3(unsigned char ZP)
{
	_cart->_romHistory[nextRomIndex++] = (0x86);
	_cart->_romHistory[nextRomIndex++] = (ZP);
}

void vcsSty3(unsigned char ZP)
{
	_cart->_romHistory[nextRomIndex++] = (0x84);
	_cart->_romHistory[nextRomIndex++] = (ZP);
}

void vcsTxs2()
{
	_cart->_romHistory[nextRomIndex++] = (0x9a);
}

void vcsJsr6( unsigned short target)
{
	_cart->_romHistory[nextRomIndex++] = (0x20);
	_cart->_romHistory[nextRomIndex++] = (target & 0xff);
	_cart->_romHistory[nextRomIndex++] = (target >> 8);
	nextJumpTarget = target;
	stuffCount = nextStuffIndex;
	nextStuffIndex = 0;
	// Wait until stella has executed the JMP
	std::unique_lock<std::mutex> lk(m);
	_runGame = false;
	_runEmulator = true;
	cv.notify_one();
	cv.wait(lk, [] {return _runGame; });
	lk.unlock();
}

void vcsNop2()
{
	_cart->_romHistory[nextRomIndex++] = (0xea);
}

// Puts nop on bus for n * 2 cycles
// Use this to perform lengthy calculations
void vcsNop2n(int n)
{
	for (int i = 0; i < n; i++)
	{
		_cart->_romHistory[nextRomIndex++] = (0xea);
	}
}

void vcsSetMasks(unsigned char * aMask, unsigned char * xMask, unsigned char * yMask)
{
	*aMask = 0;
	*xMask = 0;
	*yMask = 0;
}