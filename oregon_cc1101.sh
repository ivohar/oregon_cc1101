#! /bin/sh

### BEGIN INIT INFO
# Provides:          oregon_cc1101
# Required-Start:    
# Required-Stop:
# X-Start-Before:    
# Default-Start:     2 3 4 5
# Default-Stop:
# Short-Description: Oregon sensors read daemon 
# Description: Oregon sensors read daemon using a cc1101 radio module
### END INIT INFO

OREGONREAD_EXEC="/opt/vc/bin/oregon_read"

test -x $OREGONREAD_EXEC || exit 0

set -e

case "$1" in
  start)
    $OREGONREAD_EXEC
	;;
  reload|restart|force-reload)
	;;
  stop)
	$OREGONREAD_EXEC -K
	;;
  status)
	$OREGONREAD_EXEC -V
	;;
  *)
	echo "Usage: ${0} {start|stop|restart|force-reload|status}" >&2
	exit 1
	;;
esac

exit 0
