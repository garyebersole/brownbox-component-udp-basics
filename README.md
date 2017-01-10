# UDP Basics for ESP822-Based Component
This is the initial framework for components sending messages to a component
server listening for UDP packets on a designated port. The inline documentation
explains some of the functionality but a quick overview follows:

* On power up, the app latches a MOSFET gate to keep the ESP powered for the remaining
operations.
* Before attempting to connect to the AP, the WiFi object is configured with values that minimize the time to connect. See the code comments for details.
* The first step is to connect to the WiFi AP (*wifiConnected*). We follow the usual ESP WiFi connection model by using a 'while' loop while testing for a valid connection status.
* Once connected, we invoke a function to wait for the component server to become
ready. The function uses a 'for' loop with pre-determined values to that limit the
retry attempts. We use the *componentServerReady* function rather than simply
sending the message packet to minimize he likelihood of multiple messages being
sent for the same event.
* If the component server is ready, then we follow a similar pattern to send
the event message packet  (*eventMessageSent*).

## Component Server
This requires an active component server running on a host configured as an AP.
See the following for setting up a companion AP and the component server.

* https://gist.github.com/garyebersole/84b70af5ea7b9b46fcc2dc0bb21cdedc
* https://github.com/garyebersole/brownbox-component-server
* https://github.com/garyebersole/brownbox-component-client

##Notes

* The code is saved as a *cpp* file since I use PlatformIO rather than Arduino.
Just rename it as an *ino* file.
* This code was developed and tested on a bare ESP-12 module. It does **NOT** work
properly on a D1. Powering off drops the serial monitor and a reset reports a
message transmission time in excess of 1300ms. Power cycling the D1 still reports
a similar result.
* It's very much a work in progress but it does serve as a framework for how I
believe a battery-powered component will power up and send a message to a server.
* There may be room to take a bit more out of the message transmission time but that has
been kicked down the road since the opportunity for more gain is diminishing. At this point, the
timing results:

  * 340-350ms to boot up the ESP
  * 150-200ms to connecting to the AP
  * 20-60ms to wake up the component server
  * 10-12ms to send the message packet

* This delivers a total time of 510-620ms. I think there is still time to be
recovered in the AP connection since the AP disconnects the component even
though the AP configuration tried to keep the connection alive. Waking up
the component server is also quite variable. It's probably worth another day before
FCS to see if we can hit a target of 500ms on a consistent basis.
* After flashing on a bare ESP-12 module, disconnect GPIO00 from ground and reset
the ESP. Failure to do this means the first attempt will not connect to the AP.

##TODO

* Define and implement message protocol
* Read and report battery status
* Implement an alert module that fires a red or green LED in a specific
pattern depending on success or failure mode
* Do more testing to reduce AP connection time
