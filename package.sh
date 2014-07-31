make distclean

MAYOR=`fgrep "define ETHERIP_MAYOR" ether_ipApp/src/drvEtherIP.h | sed -e 's/.*OR //'`
MINOR=`fgrep "define ETHERIP_MINOR" ether_ipApp/src/drvEtherIP.h | sed -e 's/.*OR //'`
VERSION=ether_ip-$MAYOR-$MINOR
cd ..
gnutar vzcf $VERSION.tgz --exclude .hg ether_ip

echo
echo
echo "Now Download $VERSION to http://sourceforge.net/projects/epics ..."
