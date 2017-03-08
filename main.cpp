#include <array>
#include <boost/asio.hpp>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <linux/if_tun.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "NetPlatform.h"
#include "counter.hpp"

using namespace std;

template <typename T> void memzero(const T & obj) {
        std::memset( (void*) & obj , 0 , sizeof(T) );
}

template <typename T>
class as_zerofill : public T {
        public:
                as_zerofill() {
                        assert( sizeof(*this) == sizeof(T) ); // TODO move to static assert. sanity check. quote isostd
                        void* baseptr = static_cast<void*>( dynamic_cast<T*>(this) );
                        assert(baseptr == this); // TODO quote isostd
                        memset( baseptr , 0 , sizeof(T) );
                }
                T& get() { return *this; }
};

/******************************************************************/

class c_tun_device_linux_asio final {
	public:
		c_tun_device_linux_asio(size_t number_of_threads);
		~c_tun_device_linux_asio();
		void set_ipv6(const std::array<uint8_t, 16> &binary_address, int prefixLen, uint32_t mtu);
		void set_mtu(uint32_t mtu);
		boost::asio::posix::stream_descriptor &get_stream_descriptor();
	private:
		const int m_tun_fd;
		boost::asio::io_service m_io_service;
		boost::asio::io_service::work m_idle_work;
		boost::asio::posix::stream_descriptor m_tun_handler;
		std::vector<std::thread> m_io_service_threads;
};

c_tun_device_linux_asio::c_tun_device_linux_asio(size_t number_of_threads)
:
	m_tun_fd(open("/dev/net/tun", O_RDWR)),
	m_io_service(),
	m_idle_work(m_io_service),
	m_tun_handler(m_io_service, m_tun_fd)
{
	if (!m_tun_handler.is_open()) throw std::runtime_error("TUN is not open");
	for (size_t i = 0; i < number_of_threads; i++)
		m_io_service_threads.emplace_back([this]{m_io_service.run();});
}

c_tun_device_linux_asio::~c_tun_device_linux_asio() {
	m_io_service.stop();
	for (auto & thread : m_io_service_threads)
		thread.join();
}

void c_tun_device_linux_asio::set_ipv6(const std::array<uint8_t, 16> &binary_address, int prefixLen, uint32_t mtu) {
	as_zerofill< ifreq > ifr; // the if request
	ifr.ifr_flags = IFF_TUN; // || IFF_MULTI_QUEUE; TODO
	strncpy(ifr.ifr_name, "galaxy%d", IFNAMSIZ);
	std::cout << "iface name " << ifr.ifr_name << '\n';
	std::cout << "IFNAMSIZ " << IFNAMSIZ << '\n';
	auto errcode_ioctl =  ioctl(m_tun_fd, TUNSETIFF, static_cast<void *>(&ifr));
	if (errcode_ioctl < 0) throw std::runtime_error("ioctl error");
//	assert(binary_address[0] == 0xFD);
//	assert(binary_address[1] == 0x42);
	NetPlatform_addAddress(ifr.ifr_name, binary_address.data(), prefixLen, Sockaddr_AF_INET6);
	NetPlatform_setMTU(ifr.ifr_name, mtu);
	m_tun_handler.release();
	m_tun_handler.assign(m_tun_fd);
}

boost::asio::posix::stream_descriptor &c_tun_device_linux_asio::get_stream_descriptor() {
	return m_tun_handler;
}




/******************************************************************/

/// Were all packets received in order?
struct c_packet_check {
	c_packet_check(size_t max_packet_index);

	void see_packet(size_t packet_index);

	vector<bool> m_seen; ///< was this packet seen yet
	size_t m_count_dupli;
	size_t m_count_uniq;
	size_t m_count_reord;
	size_t m_max_index;
	bool m_i_thought_lost; ///< we thought packets are lost

	void print() const;
	bool packets_maybe_lost() const; ///< do we think now that some packets were lost?
};

c_packet_check::c_packet_check(size_t max_packet_index)
	: m_seen( max_packet_index , false ), m_max_index(0)
{ }

bool c_packet_check::packets_maybe_lost() const {
	size_t max_reodrder = 1000; // if more packets are out then it's probably lost.
	// do we have packet-index much higher then number of packets recevied at all:
	if (m_max_index > m_count_uniq + max_reodrder) return true;
	return false;
}

void c_packet_check::see_packet(size_t packet_index) {
	if (packet_index < m_max_index) {
		++ m_count_reord;
	}
	m_max_index = std::max( m_max_index , packet_index );

	if (packets_maybe_lost()) m_i_thought_lost=true;

	if (m_seen.at(packet_index)) {
		++ m_count_dupli;
		const size_t warn_max = 100;
		if (m_count_dupli < warn_max)	{
			std::cout << "duplicate at packet_index=" << packet_index << '\n';
			print();
		}
		if (m_count_dupli == warn_max)	std::cout << "duplicate at packet_index - will hide further warnings\n";
	} else { // a not-before-seen packet index
		++ m_count_uniq;
	}
	m_seen.at(packet_index) = true;
}

void c_packet_check::print() const {
	auto missing = m_max_index - m_count_uniq; // mising now. maybe will come in a moment as reordered, or maybe are really lost
	double missing_part = 0;
	if (m_max_index>0) missing_part = (double)missing / m_max_index;
	auto & out = std::cout;
	out << "Packets: uniq="<<m_count_uniq/1000<<"K ; Max="<<m_max_index
		<<" Dupli="<<m_count_dupli
		<<" Reord="<<m_count_reord
		<<" Missing(now)=" << missing << " "
			<< std::setw(3) << std::setprecision(2) << std::fixed << missing_part*100. << "%";

	if (packets_maybe_lost()) out<<" LOST-PACKETS ";
	else if (m_i_thought_lost) out<<" (packet seemed lost in past, but now all looks fine)";

	out<<endl;
}




#define global_config_end_after_packet (4*1000*1000)
const int config_buf_size = 65535 * 1;


int main() {
	c_tun_device_linux_asio tun_device(1);
	std::array<uint8_t, 16> ip_address;
	ip_address.fill(0x80);
	ip_address.at(0) = 0xFD;
	ip_address.at(1) = 0x00;
	tun_device.set_ipv6(ip_address, 8, 65500);


	std::cout << "Entering the event loop\n";
	c_counter counter    (std::chrono::seconds(1),true);
	c_counter counter_big(std::chrono::seconds(3),true);
	c_counter counter_all(std::chrono::seconds(999999),true);

	fd_set fd_set_data;

	const int buf_size = config_buf_size;
	unsigned char buf[buf_size];
	const bool dbg_tun_data=1;
	int dbg_tun_data_nr = 0; // how many times we shown it

	c_packet_check packet_check(10*1000*1000);

	bool warned_marker=false; // ever warned about marker yet?

	size_t loop_nr=0;

	while (1) {
		++loop_nr;
		ssize_t size_read_tun=0, size_read_udp=0;
		const unsigned char xorpass=42;
		auto size_read=0;
		//size_read += read(m_tun_fd, buf, sizeof(buf));
		size_read += tun_device.get_stream_descriptor().read_some(boost::asio::buffer(buf, sizeof(buf)));
		const int mark1_pos = 52;
			bool mark_ok = true;
			if (!(  (buf[mark1_pos]==100) && (buf[mark1_pos+1]==101) &&  (buf[mark1_pos+2]==102)  )) mark_ok=false;

			{ // validate counter 1
				long int packet_index=0;
				for (int i=0; i<4; ++i) packet_index += static_cast<size_t>(buf[mark1_pos+2+1 +i]) << (8*i);
				// _info("packet_index " << packet_index);

				if (packet_index >= global_config_end_after_packet ) {
						cout << "LIMIT - END " << endl << endl;
					std::cout << "Limit - ending test\n";
					break ;
				} // <====== RET

				packet_check.see_packet(packet_index);
	//			packet_stats.see_size( size_read ); // TODO
			}
			if ( buf[size_read-10] != 'X') {
				if (!warned_marker) std::cout << "Wrong marker X\n";
				warned_marker=true;
				mark_ok=false;
			}
			if ( buf[size_read-1] != 'E') {
				if (!warned_marker) std::cout << "Wrong marker E\n";
				warned_marker=true;
				mark_ok=false;
			}

	//		if (!mark_ok) _info("Packet has not expected UDP data! (wrong data read from TUN?) other then "
	//			"should be sent by our ipclient test program");

			if (dbg_tun_data && dbg_tun_data_nr<5) {
				++dbg_tun_data_nr;
				// _info("Read: " << size_read);
				auto show = std::min(size_read,128); // show the data read, but not more then some part
				auto start_pos=0;
				for (int i=0; i<show; ++i) {
					cout << static_cast<unsigned int>(buf[i]) << ' ';
					if ((buf[i]==100) && (buf[i+1]==101) && (buf[i+2]==102)) start_pos = i;
				}
				std::cout << "size_read=" << size_read << " start_pos=" << start_pos << '\n';

				cout << endl << endl;
				// buf[buf_size-1]='\0'; // hack. terminate sting to print it:
				// cout << "Buf=[" << string( reinterpret_cast<char*>(static_cast<unsigned char*>(&buf[0])), size_read) << "] buf_size="<< buf_size << endl;
			}

			size_read_tun += size_read;

		bool printed=false;
		printed = printed || counter.tick(size_read_tun, std::cout);
		bool printed_big = counter_big.tick(size_read_tun, std::cout);
		printed = printed || printed_big;
		if (printed_big) packet_check.print();
		counter_all.tick(size_read_tun, std::cout, true);
	}

	std::cout << "Loop done\n";

	std::cout << endl << endl;
	counter_all.print(std::cout);
	packet_check.print();
	return 0;
}
