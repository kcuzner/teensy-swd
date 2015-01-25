#include "Programmer.h"

#include <string>

#define ID_VENDOR 0x16c0
#define ID_PROD   0x05dc
#define STR_MANUF "kevincuzner.com"
#define STR_PROD  "SWD Adaptor"

boost::shared_ptr<Programmer> Programmer::Open()
{
    libusb_device **devices;
    libusb_context *ctx = NULL;
    int r;
    ssize_t cnt;
    unsigned char buf[256];

    r = libusb_init(&ctx);
    if (r < 0)
        return boost::shared_ptr<Programmer>();

    cnt = libusb_get_device_list(ctx, &devices);
    if (cnt < 0)
        return boost::shared_ptr<Programmer>();

    for (int i = 0; i < cnt; i++)
    {
        libusb_device_descriptor desc;
        r = libusb_get_device_descriptor(devices[i], &desc);
        if (r < 0)
            continue; //move on to the next one if this fails

        if (desc.idVendor == ID_VENDOR && desc.idProduct == ID_PROD)
        {
            libusb_device_handle* dev;
            r = libusb_open(devices[i], &dev);
            if (r < 0)
                continue; //move on to the next one

            r = libusb_get_string_descriptor_ascii(dev, desc.iManufacturer, buf, 256);
            if (r >= 0)
            {
                std::string s((char*)buf);
                if (s == STR_MANUF)
                {
                    r = libusb_get_string_descriptor_ascii(dev, desc.iProduct, buf, 256);
                    if (r >= 0)
                    {
                        s = std::string((char*)buf);
                        if (s == STR_PROD)
                        {
                            //we have matched the manufacturer and product...good enough
                            return boost::shared_ptr<Programmer>(new Programmer(ctx, dev));
                        }
                    }
                }
            }

            libusb_close(dev);
        }
    }

    libusb_exit(ctx);
    return boost::shared_ptr<Programmer>();
}

Programmer::Programmer(libusb_context *ctx, libusb_device_handle* dev)
{
    this->context = ctx;
    this->dev = dev;
}

Programmer::~Programmer()
{
    libusb_close(this->dev);
    libusb_exit(this->context);
}

void Programmer::setLed(bool on)
{
    if (on)
    {
        libusb_control_transfer(this->dev, 0x00, 0x10, 0x00, 0x00, 0, 0, 250);
    }
    else
    {
        libusb_control_transfer(this->dev, 0x00, 0x11, 0x00, 0x00, 0, 0, 250);
    }
}

int Programmer::readT(void)
{
    unsigned char buf[4];
    libusb_control_transfer(this->dev, 0x82, 0x12, 0x00, 0x00, buf, 4, 250);

    return *(int*)buf;
}

libusb_device_handle* Programmer::getDevice()
{
    return this->dev;
}
