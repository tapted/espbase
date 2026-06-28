#pragma once

// Enables ESP32 PM light sleep after a 3 second delay. Primarily this is to give a window at boot
// time to enable flashing without having to enter download mode. Uses esp_timer to schedule the PM
// configuration after the delay. 
void delayed_pm_enable(bool keep_usb_alive = false, bool disable_sleep_on_gpio0_press = true);