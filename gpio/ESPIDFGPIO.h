#pragma once

#include "IDekiGPIO.h"  // from deki-gpio

namespace DekiEsp32
{

/// GPIO pins on ESP-IDF.
///
/// Edge counting is one interrupt handler per counted pin, adding to a
/// counter the frame reads and clears with TakeEdges. The counter is a 32-bit
/// word, which the Xtensa core reads and writes in one go, so the handler and
/// the frame need no lock.
class ESPIDFGPIO : public DekiGpio::IDekiGPIO
{
public:
    bool Initialize();

    bool SetOutput(int pin, bool high) override;
    bool SetInput(int pin, DekiGpio::Pull pull) override;
    bool Write(int pin, bool high) override;
    bool Read(int pin) override;

    bool CountEdges(int pin, DekiGpio::Edge edge, DekiGpio::Pull pull) override;
    uint32_t TakeEdges(int pin) override;
    void StopCounting(int pin) override;

private:
    bool m_IsrServiceInstalled = false;
};

}  // namespace DekiEsp32
