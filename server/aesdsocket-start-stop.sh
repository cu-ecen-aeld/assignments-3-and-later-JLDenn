#!/bin/sh

echo "starting...."

case "$1" in
	start)
		echo "Starting aesdsocket"
		start-stop-daemon  -n aesdsocket -a /usr/bin/aesdsocket -S -- -d
		;;
	stop)
		echo "Stopping aesdsocket"
		start-stop-daemon -K -n aesdsocket
		;;
	*)
		echo "Usage: %0 {start|stop}"
	exit 1
esac

exit 0