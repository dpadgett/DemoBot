# DemoBot
JKA robot responsible for recording demos and interfacing between game chats and demo database.

## Requirements
curl is required to build.

To enable charset conversion on linux, the proper locales must be generated first.
You can check if the needed locales are present using:

```
$ locale -a
en_US.CP1252
en_US.UTF-8
```

To generate any missing locale, do:

```
cp /usr/share/i18n/charmaps/CP1252.gz /tmp
cd /tmp
gzip -d CP1252.gz
sudo localedef -f /tmp/CP1252 -i /usr/share/i18n/locales/en_US  /usr/lib/locale/en_US.CP1252
```

similarly for UTF-8 if it is missing.
