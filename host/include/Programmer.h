#ifndef PROGRAMMER_H
#define PROGRAMMER_H

#include <boost/smart_ptr.hpp>
#include <libusb-1.0/libusb.h>

class Programmer
{
    public:
        /**
         * Opens a programmer device
         */
        static boost::shared_ptr<Programmer> Open();

        virtual ~Programmer();

        void init(void);
        void setLed(bool on);
        int readT(void);
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
