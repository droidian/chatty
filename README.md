# Chatty

A libpurple messaging client


## Build and install

### Install dependencies

On a Debian based system run

``` bash
    sudo apt-get -y install build-essential
    sudo apt-get -y build-dep .
```

For an explicit list of dependencies check the Build-Depends entry in the
[debian/control](https://source.puri.sm/Librem5/chatty/blob/master/debian/control#5)
file.

Libhandy (libhandy-1-dev) is available in [PureOS][0] and  Debian's
[experimental distribution][1].  If you don't want to fetch it from there you
can [build it from souce][2].

### Build and install the SMS plugin
``` bash
git clone git@source.puri.sm:Librem5/purple-mm-sms.git
cd purple-mm-sms
make
make install
```

This can be skipped if SMS support is not needed.

### Build and install the 'carbons' plugin
Message synchronization between devices according to XEP-0280

``` bash
git clone https://github.com/gkdr/carbons.git
cd carbons
make
make install
```

### Build and install the 'lurch' plugin
Please go to the git page where you'll find information on how to build and use the
[lurch OMEMO plugin](https://github.com/gkdr/lurch)

This can be skipped if encrypted messaging is not needed.


### Build and install the 'purple-matrix' plugin
Please go to the git page where you'll find information on how to build and install the
[Matrix messenger plugin](https://github.com/matrix-org/purple-matrix)

This can be skipped if Matrix messaging is not needed.


### Build and install the 'purple-telegram' plugin
Please go to the git page where you'll find information on how to build and install the
[Telegram messenger plugin](https://github.com/majn/telegram-purple)

This can be skipped if Telegram messaging is not needed.


### Build and install 'libfeedback'
Please go to the git page where you'll find information on how to build and use
[Feedbackd and libfeedback](https://source.puri.sm/Librem5/feedbackd)


### Build Chatty
``` bash
meson build
ninja -C build
```

## Running from the source tree
To run Chatty from source tree (without installing) do:

``` bash
build/run
```

## Commands

In a messaging conversation (except SMS conversations) the following commands can be used:

### lurch plugin

- '/lurch help': Displays a list with available commands.
- '/lurch uninstall': Uninstalls this device from OMEMO by removing its device ID from the devicelist.
- '/lurch blacklist add': Adds conversation partner to blacklist.
- '/lurch blacklist remove': Removes conversation partner from blacklist.
- '/lurch show id own': Displays this device's ID.
- '/lurch show id list': Displays this account's devicelist.
- '/lurch show fp own': Displays this device's key fingerprint.
- '/lurch show fp conv': Displays the fingerprints of all participating devices.
- '/lurch remove id <id>': Removes a device ID from the own devicelist.


### purple-mm-sms plugin

- '/mm-sms help': Displays a list with available commands.
- '/mm-sms status': Show modem status.


## XMPP account

If you don't have an XMPP account yet and want to subscribe to a service then please make sure that the server supports the following XEPs:

- XEP-0237: Roster Versioning
- XEP-0198: Stream Management
- XEP-0280: Message Carbons
- XEP-0352: Client State Indication
- XEP-0313: Message Archive Management
- XEP-0363: HTTP File Upload

[0]: http://software.pureos.net/search_pkg?term=libhandy-1-dev
[1]: https://packages.debian.org/search?keywords=libhandy-1-dev
[2]: https://source.puri.sm/Librem5/libhandy
