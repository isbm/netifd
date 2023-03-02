FROM ubuntu:jammy

RUN apt-get update && apt-get install -y gcc cmake ninja-build pkg-config libjson-c-dev libnl-3-dev jq python3-pip iproute2 isc-dhcp-server libjson-c5 libnl-3-200 python3 udhcpc isc-dhcp-client
# pyroute2 in ubuntu-jammy is buggy, because this patch is not in: https://github.com/svinota/pyroute2/commit/94deae0ccaa016a07acb866db6d3523151b6ba8a
RUN pip3 install pyroute2
RUN touch /etc/netifd-test-container

# Workaround apparmor profiles for dhclient and dhcpd
RUN mv /sbin/dhclient /opt/dhclient && ln -sf /opt/dhclient /sbin/dhclient
RUN mv /usr/sbin/dhcpd /opt/dhcpd && ln -sf /opt/dhcpd /usr/sbin/dhcpd

WORKDIR /data/test
