#include "counter.hpp"
#include <iomanip>
#include <thread>

c_counter::c_counter(c_counter::t_duration tick_len, bool is_main)
	: m_tick_len(tick_len), m_is_main(is_main),
	m_pck_all(0), m_pck_w(0), m_bytes_all(0), m_bytes_w(0)
{
	m_time_first = m_time_ws = time_now();
}

std::chrono::steady_clock::time_point c_counter::time_now() {
	return std::chrono::steady_clock::now();
}
double c_counter::time_to_second(std::chrono::steady_clock::duration dur) {
	auto micro = std::chrono::duration_cast<std::chrono::microseconds>( dur ).count();
	return micro / static_cast<double>(1*1000*1000);
}

c_counter::t_count c_counter::get_pck_all() const { ///< read all packets count
	return m_pck_all;
}
c_counter::t_count c_counter::get_bytes_all() const { ///< read all packets bytes
	return m_bytes_all;
}

void c_counter::add(c_counter::t_count bytes) { ///< general type for integrals (number of packets, of bytes)
	m_pck_all += 1;
	m_pck_w += 1;

	m_bytes_all += bytes;
	m_bytes_w += bytes;
}

void c_counter::reset_time() {
	m_time_first = time_now();
	m_time_ws = m_time_last = time_now();
}

bool c_counter::tick(c_counter::t_count bytes, std::ostream &out, bool silent) {
	add(bytes);

	bool do_print=0;
	bool do_reset=0;
	if (m_pck_all==1) { // first packet
		do_print=1; do_reset=1;

		m_time_first = time_now(); // and other times in reset
	}
	if (0 == (m_pck_all%(1000*10))) {
		// std::cerr<<m_bytes_all<<std::endl; std::cout << m_time_last << " >? " << m_time_ws << std::endl;
		m_time_last = time_now();
		if (m_time_last >= m_time_ws + m_tick_len) { do_reset=1; do_print=1; }
	}
	if (silent) do_print=false;
	if (do_print) print(out);
	if (do_reset) {
		m_time_ws = m_time_last = time_now();
		m_pck_w=0;
		m_bytes_w=0;
	}
	return do_print;
}

void c_counter::print(std::ostream &out) const { ///< prints now the statistics (better instead call tick)
	using std::setw;

	double time_all = time_to_second( m_time_last - m_time_first );
	double time_w   = time_to_second( m_time_last - m_time_ws );

	double avg_bytes_all = m_bytes_all / time_all;
	double avg_pck_all   = m_pck_all   / time_all;
	double avg_bytes_w = m_bytes_w / time_w;
	double avg_pck_w   = m_pck_w   / time_w;

	// formatting
	int p1=1; // digits after dot
	int w1=p1+1+5; // width

	double K=1000, Mi = 1024*1024, Gi = 1024*Mi; // units

	out << "[thread " << std::this_thread::get_id() << "] ";

	out	<< std::setprecision(3) << std::fixed;

	if (false) std::cerr << "Debug: m_bytes_all="<<m_bytes_all
		<< " m_bytes_w="<<m_bytes_w
		<< " time_all="<<time_all
		<< " time_w="<<time_w
		<< std::endl;
		;

	if (m_is_main) {
		if (time_all > 0) {
			out << "Sent " << setw(6) << m_bytes_all/Gi << "GiB; "
					<< " in " << setw(6) << m_pck_all << " pck; "
					<< "Speed: "
									 << setw(w1) << (avg_pck_all / K) << " Kpck/s "
					<< ",  " << setw(w1) << (avg_bytes_all*8 / Mi) << " Mib/s "
					<< " = " << setw(w1) << (avg_bytes_all   / Mi) << " MiB/s " << "; "
			;
		} else out << "(no time_all yet); ";
	}

	if (time_w > 0) {
		out << "Window " << time_w << "s: "
		             << setw(w1) << (avg_pck_w   / 1000) << " Kpck/s "
		    << ",  " << setw(w1) << (avg_bytes_w  *8 / Mi) << " Mib/s "
		    << " = " << setw(w1) << (avg_bytes_w     / Mi) << " MiB/s " << "; ";
	} else out << "(no time_w yet); ";

	out << std::endl;
}




