# Part XVI — Functional Verification in Wokwi

The following tests were performed using the Wokwi simulation of the STM32 FreeRTOS multisensor room-monitoring system.

| Test ID | Input / Stimulus | Expected Result | Actual Observed Result | Result |
|---|---|---|---|---|
| FT-01 | Change DHT22 temperature to 27.3 °C | Displayed temperature updates | Serial Monitor and OLED both updated to 27.3 °C | PASS |
| FT-02 | Change DHT22 humidity to 77.0% | Displayed humidity updates | OLED humidity page updated to 77.0% | PASS |
| FT-03 | Change photoresistor illumination to 2291 lux | Light value changes | OLED light page updated to 10% | PASS |
| FT-04 | Rotate encoder clockwise from LIGHT | Next page is selected | Display changed from LIGHT to MOTION | PASS |
| FT-05 | Rotate encoder counterclockwise from MOTION | Previous page is selected | Display changed from MOTION to LIGHT | PASS |
| FT-06 | Set DHT22 temperature to 35.8 °C | Alarm activates | AlarmTask reported HIGH TEMPERATURE, buzzer turned ON, and EVENT_ALARM was received | PASS |
| FT-07 | Return DHT22 temperature to 23.9 °C | Alarm stops | AlarmTask reported NORMAL and buzzer turned OFF | PASS |
| FT-08 | Trigger PIR motion while system is ACTIVE | System is ACTIVE | PIR motion was detected and sensor/display data showed Motion: YES while the system remained ACTIVE | PASS |
| FT-09 | Allow 15 seconds without PIR motion | System becomes INACTIVE | System changed to INACTIVE, OLED was blanked, and EVENT_INACTIVE was received | PASS |
| FT-10 | Trigger PIR while system is INACTIVE | System returns to ACTIVE | PIR was detected, system returned to ACTIVE, OLED was restored, and EVENT_ACTIVE/EVENT_MOTION were received | PASS |

## Summary

All 10 required functional verification tests passed successfully in Wokwi.