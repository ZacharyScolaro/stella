#ifndef CARTSTRONGARMDEV_HXX
#define CARTSTRONGARMDEV_HXX

class System;

#include "bspf.hxx"
#include "Cart.hxx"

#include <thread>
#include <mutex>

uInt16 RunStrongArmGame();

class CartStrongArmDev : public Cartridge
{
  public:
    /**
      Create a new cartridge using the specified image and size

      @param image     Pointer to the ROM image
      @param size      The size of the ROM image
      @param settings  A reference to the various settings (read-only)
    */
	  CartStrongArmDev(const BytePtr& image, uInt32 size, const Settings& settings);
    virtual ~CartStrongArmDev() = default;

  public:
    /**
      Reset device to its power-on state
    */
    void reset() override;

    /**
      Install cartridge in the specified system.  Invoked by the system
      when the cartridge is attached to it.

      @param system The system the device should install itself in
    */
    void install(System& system) override;


	 bool patch(uInt16 address, uInt8 value) override;

    /**
      Access the internal ROM image for this cartridge.

      @param size  Set to the size of the internal ROM image data
      @return  A pointer to the internal ROM image data
    */
    const uInt8* getImage(uInt32& size) const override;

    /**
      Save the current state of this cart to the given Serializer.

      @param out  The Serializer object to use
      @return  False on any errors, else true
    */
    bool save(Serializer& out) const override;

    /**
      Load the current state of this cart from the given Serializer.

      @param in  The Serializer object to use
      @return  False on any errors, else true
    */
    bool load(Serializer& in) override;

    /**
      Get a descriptor for the device name (used in error checking).

      @return The name of the object
    */
    string name() const override { return "StrongArmDev"; }

  public:
    /**
      Get the byte at the specified address

      @return The byte at the specified address
    */
    uInt8 peek(uInt16 address) override;

    /**
      Change the byte at the specified address to the given value

      @param address The address where the value should be stored
      @param value The value to be stored at the address
      @return  True if the poke changed the device address space, else false
    */
    bool poke(uInt16 address, uInt8 value) override;

	uInt8 _romHistory[4096];
private:
	std::thread * _gameThread;

  private:
    // Following constructors and assignment operators not supported
    CartStrongArmDev() = delete;
    CartStrongArmDev(const CartStrongArmDev&) = delete;
    CartStrongArmDev(CartStrongArmDev&&) = delete;
    CartStrongArmDev& operator=(const CartStrongArmDev&) = delete;
    CartStrongArmDev& operator=(CartStrongArmDev&&) = delete;
};

#endif
