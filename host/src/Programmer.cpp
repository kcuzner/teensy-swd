#include "Programmer.h"

#include <string>

#include "../../shared/usb_types.h"

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

int Programmer::setLed(bool on)
{
    if (on)
    {
        return libusb_control_transfer(this->dev, 0x00, 0x10, 0x00, 0x00, 0, 0, 250);
    }
    else
    {
        return libusb_control_transfer(this->dev, 0x00, 0x11, 0x00, 0x00, 0, 0, 250);
    }
}

int Programmer::queueRead(uint8_t request, uint8_t index)
{
    read_req_t read = {
        .request = request
    };

    return libusb_control_transfer(this->dev, 0x00, USB_SWD_BEGIN_READ >> 8, 0x00, index, (unsigned char*)(&read), sizeof(read), 250);
}

int Programmer::queueWrite(uint8_t request, uint32_t data, uint8_t index)
{
    write_req_t write = {
        .request = request,
        .data = data
    };

    return libusb_control_transfer(this->dev, 0x00, USB_SWD_BEGIN_WRITE >> 8, 0x00, index, (unsigned char*)(&write), sizeof(write), 250);
}

int Programmer::getResult(uint8_t index, swd_result_t* dest)
{
    return libusb_control_transfer(this->dev, 0x80, USB_SWD_READ_STATUS >> 8, 0x00, index, (unsigned char*)dest, sizeof(swd_result_t), 250);
}

libusb_device_handle* Programmer::getDevice()
{
    return this->dev;
}
