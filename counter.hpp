#include <chrono>
#include <iostream>

class c_counter {
	public:
		typedef long long int t_count;
		typedef std::chrono::steady_clock::time_point t_timepoint;
		typedef std::chrono::duration<double> t_duration;

		c_counter(c_counter::t_duration tick_len, bool is_main); ///< tick_len - how often should we fire (print stats, and restart window)

		void add(c_counter::t_count bytes); ///< general type for integrals (number of packets, of bytes)
		bool tick(c_counter::t_count bytes, std::ostream &out, bool silent=false); ///< tick: add data; update clock; print; return - was print used

		void reset_time(); ///< resets the time to current clock (but keeps number of bytes)

		void print(std::ostream &out) const; ///< prints now the statistics

		t_count get_pck_all() const; ///< read all packets count
		t_count get_bytes_all() const; ///< read all bytes count

	private:
		const t_duration m_tick_len; ///< how often should I tick - it's both the window size, and the rate of e.g. print()

		bool m_is_main; ///< is this the main counter (then show global stats and so on)

		t_count m_pck_all, m_pck_w; ///< packets count: all, and in current window
		t_count m_bytes_all, m_bytes_w; ///< the bytes (all, and in current windoow)

		t_timepoint m_time_first; ///< when I was started at first actually
		t_timepoint m_time_ws; ///< window stared time
		t_timepoint m_time_last; ///< current last time

		static std::chrono::steady_clock::time_point time_now();
		static double time_to_second(std::chrono::steady_clock::duration dur);
};

