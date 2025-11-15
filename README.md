Purpose is to send a mail and a picture when something is put in my physical mailbox.
This project is a new version of a mail detector i designed some time ago and that one served well for over 7 years.
The original version had an ESP8266, no camera, in 7 years i had to replace a reed contact due to poor quality and oxidation and update the software twice because the mail authenticiation for gmail changed.

My problem, i have a large brick mailbox in front of my house with a mail slot and in the back a separate door and storage for larger deliveries, i'm surprised how many notice the compartment and use it.

Mail, advertising, online order delivery... these days it can be at any time, sometimes they don't push it all the way in and when it rains i get soggy mail.

So i wanted a warning system that sends me a mail when something is put in either the mailbox or "box" box.

One of the challenges is that the mail slot is open, there is no flap or anything that moves when mail is put in.

The original concept was based on ESP8266 and a ultrasonic sensor HC04 (3.3v version!), it is mounted above the slot and measures the distance to the floor of the mailbox, if the distance is short it means something is passing in the slot and it then activates wifi and sends a mail. Both doors have a reed sensor so i also get a notice when the box compartment is opened. I included a few leds to light the mailbox inside when i open the door, very handy at night, when i open the door that ignores any triggers that might happen when i pull stuck mail out of the flap. Of course a lot of extras such as stuck mail detection or door that is still open etc...

One of the challenges was power, i did have a socarex pipe nearby that only had the TV cable so i made an opening and ran a cable though it to my garage where a power supply is hooked up, to allow for a long cable i placed the 5v regulator in the mailbox so losses are no issue.

The version published here is a new design based on a ESP32-CAM and can take a picture of the mail, the same ultrasonic sensor is used, i changed the reed switches to a sealed version.

As usuall i use Arduino 1 IDE in portable mode to avoid the many issues with breaking changes in libraries.
I use the dedicated programming board they sell for the ESP32-CAM module since it has no USB-serial bridge on it.

Standard disclaimer: it works for me, i have no responsibility over what you make, use as inspiration or idea for your own project.









