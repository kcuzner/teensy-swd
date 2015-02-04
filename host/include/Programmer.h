#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <boost/smart_ptr.hpp>
#include <libusb-1.0/libusb.h>

extern "C"
{
    typedef struct {
        uint8_t done;
        int8_t result;
        uint32_t data;
    } swd_result_t;
}

class Programmer
{
    public:
        /**
         * Opens a programmer device
         */
        static boost::shared_ptr<Programmer> Open();

        virtual ~Programmer();

        int setLed(bool on);
        int queueRead(uint8_t request, uint8_t index);
        int queueWrite(uint8_t request, uint32_t data, uint8_t index);
        int getResult(uint8_t index, swd_result_t* dest);
    protected:
        /**
         * Device should be constructed by calling Open()
         */
        Programmer(libusb_context*, libusb_device_handle*);

        libusb_device_handle* getDevice();
    private:

        libusb_context *context;
        libusb_device_handle *dev;
};

#endif // PROGRAMMER_H
