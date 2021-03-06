#pragma once
#include <iomanip>
#include <map>
#include "async_transport.h"
#include "zed_net.h"

//#define SHORYU_ENABLE_LOG

namespace shoryu
{
	// This will be combined with side, so ensure:
	// 0 <= side        <=  7 (3 bits)
	// 0 <= MessageType <= 31 (5 bits)
	enum class MessageType : uint8_t
	{
		None = 0,
		Frame,
		Data,
		Ping, //for pinging
		Join,
		Deny,
		Info, //side, all endpoints, delay
		Delay, //set delay
		Ready, //send to eps, after all eps answered - start the game
		MCDSync,
		EndSession,
		Chat
	};

	const char *messageTypeNames[] = {
		"None  ",
		"Frame ",
		"Data  ",
		"Ping  ",
		"Join  ",
		"Deny  ",
		"Info  ",
		"Delay ",
		"Ready ",
        "MCDsyn",
		"EndSn ",
		"Chat  "
	};


	struct message_data
	{
		std::shared_ptr<char> p;
		uint32_t data_length;
	};

	template<typename T, typename StateType>
	struct message
	{
		typedef std::vector<zed_net_address_t> endpoint_container;

		message() : frame_id(0), side(0) {}

		message(MessageType type) : cmd(type), frame_id(0), side(0)
		{
		}
		MessageType cmd;
		StateType state;
		int64_t frame_id;
		std::vector<userinfo> usernames;
		zed_net_address_t host_ep;
		uint32_t rand_seed;
		uint8_t delay;
		uint8_t side;
        bool mcdsync;
		uint8_t num_players;
		T frame;
		message_data data;
		std::string username;
		std::string lobby_message;
		
		inline void serialize(shoryu::oarchive& a) const
		{
			uint8_t cmdSide = ((int)cmd & 0x1F) | ((side & 0x07) << 5);
			a << cmdSide;
			size_t length;
			switch(cmd)
			{
			case MessageType::Join:
				a << state << host_ep.host << host_ep.port;
				length = username.length();
				a << length;
				if(length)
					a.write((char*)username.c_str(), username.length());
				break;
			case MessageType::Data:
				a << frame_id << data.data_length;
				a.write(data.p.get(), data.data_length);
			case MessageType::Deny:
				a << state;
				break;
			case MessageType::Frame:
				// 24 bits gives us 16777216 frames
				// assuming each frame is 1/60th of a second
				// 24 bits is enough for a little over 3 days of session time
				uint8_t framePart;
				framePart = frame_id & 0xFF;
				a << framePart;
				framePart = (frame_id >> 8) & 0xFF;
				a << framePart;
				framePart = (frame_id >> 16) & 0xFF;
				a << framePart;
				frame.serialize(a);
				break;
			case MessageType::Info:
				a << rand_seed << side << mcdsync << num_players;
				for(size_t i = 0; i < num_players; i++)
				{
					length = usernames[i].name.length();
					a << length;
                    if (length)
                        a.write((char *)usernames[i].name.c_str(), usernames[i].name.length());
                    length = usernames[i].ping.length();

                    a << length;
                    if (length)
						a.write((char *)usernames[i].ping.c_str(), usernames[i].ping.length());

                    a << usernames[i].side;
				}
				a << state;
				break;
			case MessageType::Delay:
				a << delay;
				break;
			case MessageType::Chat:
				length = username.length();
				a << length;
				if (length)
					a.write((char*)username.c_str(), length);
				length = lobby_message.length();
				a << length;
				a.write((char*)lobby_message.c_str(), length);
				break;
			default:
				break;
			}
		}
		inline void deserialize(shoryu::iarchive& a)
		{
			uint8_t cmdSide;
			a >> cmdSide;
			cmd = (MessageType)(cmdSide & 0x1F);
			side = cmdSide >> 5;
			unsigned long addr;
			unsigned short port;
			switch(cmd)
			{
			case MessageType::Join:
				a >> state >> addr >> port;
				host_ep.host = addr;
				host_ep.port = port;
				size_t length;
				a >> length;
				if(length > 0)
				{
					std::auto_ptr<char> str(new char[length]);
					a.read(str.get(), length);
					username.assign(str.get(), str.get()+length);
				}
				break;
			case MessageType::Data:
				a >> frame_id >> data.data_length;
				data.p.reset(new char[data.data_length], std::default_delete<char[]>());
				a.read(data.p.get(), data.data_length);
			case MessageType::Deny:
				a >> state;
				break;
			case MessageType::Frame:
				// 24 bits gives us 16777216 frames
				// assuming each frame is 1/60th of a second
				// 24 bits is enough for a little over 3 days of session time
				uint8_t framePart;
				a >> framePart;
				frame_id = framePart;
				a >> framePart;
				frame_id |= framePart << 8;
				a >> framePart;
				frame_id |= framePart << 16;
				frame.deserialize(a);
				break;
			case MessageType::Info:
				a >> rand_seed >> side >> mcdsync >> num_players;
				repeat(num_players)
				{
					size_t length;
                    userinfo _userinfo;
					a >> length;
					if(length > 0)
					{
						std::auto_ptr<char> str(new char[length]);
						a.read(str.get(), length);
                        _userinfo.name = std::string(str.get(), str.get() + length);

						a >> length;
                        str.reset(new char[length]);
                        a.read(str.get(), length);
                        _userinfo.ping = std::string(str.get(), str.get() + length);

						a >> _userinfo.side;
                        usernames.push_back(_userinfo);
                    }
					else
                        usernames.push_back(_userinfo);
				}
				a >> state;
				break;
			case MessageType::Delay:
				a >> delay;
				break;
			case MessageType::Chat:
			{
				a >> length;
				std::auto_ptr<char> str(new char[length]);
				a.read(str.get(), length);
				username.assign(str.get(), str.get() + length);

				a >> length;
				str.reset(new char[length]);
				a.read(str.get(), length);
				lobby_message.assign(str.get(), str.get() + length);
			}
				break;
			default:
				break;
			}
		}
	};

	template<typename FrameType, typename StateType>
	class session : std::noncopyable
	{
		typedef message<FrameType, StateType> message_type;
		typedef std::vector<zed_net_address_t> endpoint_container;
		typedef std::unordered_map<int64_t, FrameType> frame_map;
		typedef std::vector<frame_map> frame_table;
		typedef std::function<bool(const StateType&, const StateType&)> state_check_handler_type;
		typedef std::vector<std::unordered_map<int64_t, message_data>> data_table;
	public:
#ifdef SHORYU_ENABLE_LOG
		std::fstream log;
		msec log_start;
#endif
		session() 
		{
#ifdef SHORYU_ENABLE_LOG
			std::string filename;

			filename = "shoryu.";
			filename += std::to_string(time_ms());
			filename += ".log";

			log.open(filename, std::ios_base::trunc | std::ios_base::out);
			log_start = time_ms();
#endif
			clear();
		}

		bool bind(int port)
		{
			_async.start(port, 2);
			return true;
		}
		void unbind()
		{
			_async.stop();
		}

		void ping_clients()
		{
			while(m_ping_clients)
			{
				{
					std::unique_lock<std::mutex> lock(_connection_mutex);

					int old_player_num = m_num_players;

					// kick anyone whom we last received a message from over 5 seconds ago
					for (auto i_ep = m_clientEndpoints.begin(); i_ep != m_clientEndpoints.end();)
					{
						// update ping value
                        _username_map[*i_ep].ping = std::to_string(_async.peer(*i_ep).rtt_avg) + std::string(" ms");

						// send info
                        queue_info();

						if (_async.peer(*i_ep).recv_time + 5000 < time_ms())
						{
#ifdef SHORYU_ENABLE_LOG
							log << "[" << time_ms() - log_start << "] kicking " << zed_net_host_to_str(i_ep->host) << ":" << i_ep->port << "\n";
#endif
							m_num_players--;
							m_clientEndpoints.erase(i_ep);
						}
						else
							i_ep++;
					}

					// requeue info if player numbers changed
					if (old_player_num != m_num_players)
						queue_info();

					// queue pings to everyone
					if (m_num_players)
					{
						message_type msg(MessageType::Ping);
						queue_message(msg);
					}

					send();
				}
				std::this_thread::sleep_for(std::chrono::seconds(1));
			}
		}

		bool wait_for_mcd()
        {
            if (m_host) {
                throw std::exception("invalid caller");
            } else {
                auto pred = [&]() -> bool {
                    if (_current_state != MessageType::Ready)
                        return true;

                    if (!mcd_sync_begin) {
                        return false;
                    }
                    return true;
                };

                std::unique_lock<std::mutex> lock(_connection_mutex);
                while (!pred())
                    _connection_cv.wait_for(lock, std::chrono::seconds(1), pred);
            }

            if (_current_state != MessageType::Ready)
                return false;

            // switch to new handlers
            connection_established();

            return true;
        }

		bool wait_for_start(const zed_net_address_t &ep)
		{
			if (m_host)
			{
				auto pred = [&]() -> bool {
					if (_current_state != MessageType::Ready)
						return true;

					for (auto &ep : m_clientEndpoints)
					{
						if (std::find(m_ready_list.begin(), m_ready_list.end(), ep) == m_ready_list.end())
						{
#ifdef SHORYU_ENABLE_LOG
							log << "[" << time_ms() - log_start << "] " << zed_net_host_to_str(ep.host) << ":" << ep.port << " is not ready\n";
#endif
							return false;
						}
					}
					return true;
				};

				std::unique_lock<std::mutex> lock(_connection_mutex);
				while (!pred())
					_connection_cv.wait_for(lock, std::chrono::seconds(1), pred);
			}
			else
			{
				// send ready
                _current_state = MessageType::Ready;
                _async.queue(ep, message_type(MessageType::Ready));
                send(ep);
                _connection_cv.notify_all();

				auto pred = [&]() -> bool {
					if (_current_state != MessageType::Ready)
						return true;

					if (!m_ready)
					{
#ifdef SHORYU_ENABLE_LOG
						log << "[" << time_ms() - log_start << "] I am not ready!\n";
#endif
						return false;
					}
					return true;
				};

				std::unique_lock<std::mutex> lock(_connection_mutex);
				while (!pred())
					_connection_cv.wait_for(lock, std::chrono::seconds(1), pred);
			}

			if (_current_state != MessageType::Ready)
				return false;

			// switch to new handlers
			connection_established();

			// stop ping thread
			m_ping_clients = false;
			if (m_ping_thread && m_ping_thread->joinable())
			{
				m_ping_thread->join();
				m_ping_thread.reset();
			}

			return true;
		}

		bool create(int players, const StateType& state, const state_check_handler_type& handler, int timeout = 0)
		{
			_shutdown = false;
			try_prepare();
			m_host = true;
			m_num_players = 1;
            m_mcd_sync = g_Conf->Netplay.MemcardSync;
			if(m_userlist_handler)
			{
				std::vector<userinfo> list;
				list.push_back(_username);
				m_userlist_handler(list);
			}

			_state = state;
			_state_check_handler = handler;
			_async.receive_handler([&](const zed_net_address_t& ep, message_type& msg){create_recv_handler(ep, msg);});
			bool connected = true;
			if(create_handler() && _current_state != MessageType::None)
			{
				//connection_established();
			}
			else
			{
				connected = false;
				_current_state = MessageType::None;
				_async.receive_handler([&](const zed_net_address_t& ep, message_type& msg){recv_hdl(ep, msg);});
			}

			return connected;
		}
		bool join(zed_net_address_t ep, const StateType& state, const state_check_handler_type& handler, int timeout = 0)
		{
			_shutdown = false;
			try_prepare();
			m_host = false;
			_state = state;
			_state_check_handler = handler;
			_async.receive_handler([&](const zed_net_address_t& ep, message_type& msg){join_recv_handler(ep, msg);});
			bool connected = true;
			if(join_handler(ep, timeout) && _current_state != MessageType::None)
			{
				//connection_established();
			}
			else
			{
				connected = false;
				_current_state = MessageType::None;
				_async.receive_handler([&](const zed_net_address_t& ep, message_type& msg){recv_hdl(ep, msg);});
			}

			return connected;
		}

		inline void queue_message(message_type &msg)
		{
#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] ";
			log << messageTypeNames[(int)msg.cmd] << std::setw(7) << msg.frame_id;
			log << " (" << _side << ") --^";
#endif
			if (!m_host)
			{
				_async.queue(_host_ep, msg);
#ifdef SHORYU_ENABLE_LOG
				log << " (0) " << zed_net_host_to_str(_host_ep.host) << ":" << (int)_host_ep.port;
#endif
			}
			else
			{
				for (size_t i = 0; i < m_clientEndpoints.size(); i++)
				{
					_async.queue(m_clientEndpoints[i], msg);
#ifdef SHORYU_ENABLE_LOG
				log << " (" << i << ") " << zed_net_host_to_str(m_clientEndpoints[i].host) << ":" << (int)m_clientEndpoints[i].port;
#endif
				}
			}

#ifdef SHORYU_ENABLE_LOG
			log << "\n";
#endif
		}

		inline void clear_queue()
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			std::unique_lock<std::mutex> lock(_mutex);
			if (!m_host)
			{
				_async.clear_queue(_host_ep);
			}
			else
			{
				for (auto &ep : m_clientEndpoints)
					_async.clear_queue(ep);
			}
		}

		inline void send_end_session_request()
		{
			_end_session_request = true;
			std::unique_lock<std::mutex> lock(_mutex);
			message_type msg(MessageType::EndSession);

			queue_message(msg);
			send();
		}
		inline bool end_session_request()
		{
			return _end_session_request;
		}

		inline void reannounce_delay()
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			std::unique_lock<std::mutex> lock(_mutex);
			message_type msg(MessageType::Delay);
			msg.delay = delay();

			queue_message(msg);
			send();
		}

		inline void announce_mcd()
        {
            std::unique_lock<std::mutex> lock(_mutex);
            message_type msg(MessageType::MCDSync);

            queue_message(msg);
            send();
        }

		inline void queue_info()
		{
			message_type msg(MessageType::Info);
			msg.rand_seed = (uint32_t)time(0);
			msg.state = _state;
            msg.mcdsync = m_mcd_sync;
			msg.num_players = m_num_players;
			msg.usernames.push_back(_username);
			for (auto &ep : m_clientEndpoints)
               msg.usernames.push_back(_username_map[ep]);
			if (m_userlist_handler)
				m_userlist_handler(msg.usernames);

			srand(msg.rand_seed);
			for (size_t i = 0; i < m_clientEndpoints.size(); i++)
			{
				auto &ep = m_clientEndpoints[i];
				msg.side = i + 1;
                _username_map[ep].side = msg.side;
				_async.queue(ep, msg);
#ifdef SHORYU_ENABLE_LOG
				log << "[" << time_ms() - log_start << "] Info --^ " << zed_net_host_to_str(ep.host) << ":" << ep.port << "\n";
#endif
			}
		}

		inline void queue_data(message_data& data)
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			std::unique_lock<std::mutex> lock(_mutex);
			message_type msg(MessageType::Data);
			msg.data = data;
			msg.frame_id = _data_index++;

			queue_message(msg);
		}

		inline bool get_data(int side, message_data& data, int timeout = 0)
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");

			std::unique_lock<std::mutex> lock(_mutex);
			auto pred = [&]() -> bool {
				if(_current_state != MessageType::None)
					return _data_table[side].find(_data_index) != _data_table[side].end();
				else
					return true;
			};
			if(timeout > 0)
			{
				if(!_data_cond.wait_for(lock, std::chrono::milliseconds(timeout), pred))
					return false;
			}
			else
				_data_cond.wait(lock, pred);

			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			data = _data_table[side][_data_index];
			_data_table[side].erase(_data_index);
			++_data_index;
			return true;
		}
		
		inline void set(const FrameType& frame)
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			std::unique_lock<std::mutex> lock(_mutex);

			int64_t destFrame = _frame;

			if (m_host && (g_Conf->Netplay.ClientOnlyDelay)) {
                destFrame += 1;
            } else {
                destFrame += _delay;
            }

			_frame_table[_side][destFrame] = frame;
			message_type msg(MessageType::Frame);
			msg.frame_id = destFrame;
			msg.frame = frame;
			msg.side = _side;
			queue_message(msg);
			send();
		}
		inline int send()
		{
			int n = 0;
			if (!m_host)
			{
				n += send(_host_ep);
			}
			else
			{
				for (auto &ep : m_clientEndpoints)
					n += send(ep);
			}
			return n;
		}

		inline int send(const zed_net_address_t& ep)
		{
			return _async.send(ep);
		}
		inline bool get(int side, FrameType& f, int64_t frame, int timeout)
		{
			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			if(frame < _delay)
				return true;
			std::unique_lock<std::mutex> lock(_mutex);

			auto pred = [&]() -> bool {
				if(_current_state != MessageType::None)
					return _frame_table[side].find(frame) != _frame_table[side].end();
				else
					return true;
			};

#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] Waiting for frame " << frame << " side " << side << "\n";
#endif
			if(timeout > 0)
			{
				if (!_frame_cond.wait_for(lock, std::chrono::milliseconds(timeout), pred))
				{
#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] Waiting timeout!\n";
#endif
					return false;
				}
			}
			else
				_frame_cond.wait(lock, pred);

#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] Waiting success!\n";
#endif

			if(_current_state == MessageType::None)
				throw std::exception("invalid state");
			f = _frame_table[side][frame];

			// we accessed this frame, so should be safe to delete previous frame
			_frame_table[side].erase(frame - 1);

			return true;
		}

		inline bool get(int side, FrameType& f, int timeout)
		{
			return get(side, f, _frame, timeout);
		}

		FrameType get(int side)
		{
			FrameType f;
			get(side, f, 0);
			return f;
		}
		void delay(int d)
		{
			_delay = d;
		}
		int delay()
		{
			return _delay;
		}
		void next_frame()
		{
			_frame++;
		}
		int64_t frame()
		{
			return _frame;
		}
		void frame(int64_t f)
		{
			_frame = f;
		}
		int side()
		{
			return _side;
		}
        bool mcd_sync()
        {
            return m_mcd_sync;
        }
		bool _shutdown;
		void shutdown()
		{
			_shutdown = true;
			clear();
			_frame_cond.notify_all();
			_connection_cv.notify_all();
		}
		int port()
		{
			return _async.port();
		}
		MessageType state()
		{
			return _current_state;
		}
		int num_players()
		{
			return m_num_players;
		}
		int64_t first_received_frame()
		{
			return _first_received_frame;
		}
		int64_t last_received_frame()
		{
			return _last_received_frame;
		}
		const std::string& last_error()
		{
			std::unique_lock<std::mutex> lock(_error_mutex);
			return _last_error;
		}
		void last_error(const std::string& err)
		{
			std::unique_lock<std::mutex> lock(_error_mutex);
			_last_error = err;
		}
		const std::string& username(const zed_net_address_t& ep)
		{
			return _username_map[ep];
		}
		const std::string& username()
		{
			return _username.name;
		}
		void username(const std::string& name)
		{
			_username.name = name;
            _username.side = 0;
		}
		inline void userlist_handler(std::function<const void(std::vector<userinfo>)> handler)
		{
			m_userlist_handler = handler;
		}
		inline void set_chatmessage_handler(std::function<const void(std::string, std::string)> handler)
		{
			m_chatmessage_handler = handler;
		}
		inline void send_chatmessage(const std::string &message)
		{
			message_type msg(MessageType::Chat);
			msg.username = _username.name;
			msg.lobby_message = message;
			queue_message(msg);
			send();
			_connection_cv.notify_all();
		}
	protected:
		void try_prepare()
		{
			clear();
		}
		void clear()
		{
			_username_map.clear();
			m_ready_list.clear();
			m_ready = false;
			_last_received_frame = -1;
			_first_received_frame = -1;
			_delay = _side = /*_players =*/ 0;
			_frame = 0;
			_data_index = 0;
			_current_state = MessageType::None;
			m_host = false;
			m_clientEndpoints.clear();
			_end_session_request = false;
			_frame_table.clear();
			_last_error = "";
			_data_table.clear();
			_async.error_handler(std::function<void(const std::error_code&)>());
			_async.receive_handler(std::function<void(const zed_net_address_t&, message_type&)>());
			m_ping_clients = false;
			if (m_ping_thread && m_ping_thread->joinable())
			{
				m_ping_thread->join();
				m_ping_thread.reset();
			}
		}
		void connection_established()
		{
#ifdef SHORYU_ENABLE_LOG
			if (m_host)
				for (zed_net_address_t& ep : m_clientEndpoints)
					log << "client " << zed_net_host_to_str(ep.host) << ":" << ep.port << "\n";
			else
				log << "host" << zed_net_host_to_str(_host_ep.host) << ":" << _host_ep.port << "\n";

			log << "players: " << m_num_players << "\n";
#endif
			std::unique_lock<std::mutex> lock1(_connection_mutex);
			std::unique_lock<std::mutex> lock2(_mutex);
			_frame_table.resize(m_num_players);
			_data_table.resize(m_num_players);
			_async.error_handler([&](const std::error_code &error){err_hdl(error);});
			_async.receive_handler([&](const zed_net_address_t& ep, message_type& msg){recv_hdl(ep, msg);});
		}
		int calculate_delay(uint32_t rtt)
		{
			return (rtt / 32) + 1;
		}

		struct peer_info
		{
			MessageType state;
			uint64_t time;
			int delay;
		};

		typedef std::map<zed_net_address_t, peer_info> state_map;

		MessageType _current_state;	
		zed_net_address_t _host_ep;
		//std::semaphore _connection_sem;
		std::mutex _connection_mutex;
		std::condition_variable _connection_cv;
		static const int connection_timeout = 1000;
		StateType _state;
		state_check_handler_type _state_check_handler;
		int64_t _first_received_frame;
		int64_t _last_received_frame;

		std::function<const void(std::string username, std::string message)> m_chatmessage_handler;

		bool create_handler()
		{
			_current_state = MessageType::Ready;
			m_ping_clients = true;
			m_ping_thread.reset(new std::thread(&session::ping_clients, this));
			return true;
		}
		void create_recv_handler(const zed_net_address_t& ep, message_type& msg)
		{
#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] ";
			log << messageTypeNames[(int)msg.cmd] << std::setw(7) << msg.frame_id;
			log << " (" << _side << ") <--";
			log << " (" << (int)msg.side << ") " << zed_net_host_to_str(ep.host) << ":" << (int)ep.port;
			log << "\n";
#endif
			std::unique_lock<std::mutex> lock(_connection_mutex);
			if(msg.cmd == MessageType::Join)
			{
				_username_map[ep].name = msg.username;
                _username_map[ep].side = m_clientEndpoints.size() + 1;

				if(!_state_check_handler(_state, msg.state))
				{
					message_type msg(MessageType::Deny);
					msg.state = _state;
					_async.queue(ep, msg);
					send(ep);
					_connection_cv.notify_all();
#ifdef SHORYU_ENABLE_LOG
					log << "[" << time_ms() - log_start << "] Deny -> " << zed_net_host_to_str(ep.host) << ":" << ep.port << "\n";
#endif
					return;
				}

				if (std::find(m_clientEndpoints.begin(), m_clientEndpoints.end(), ep) == m_clientEndpoints.end())
				{
					m_clientEndpoints.push_back(ep);
					m_num_players++;
				}

				if (m_num_players)
				{
					queue_info();
					send();
					_side = 0;
				}
			}
			if(msg.cmd == MessageType::Ping)
			{
				message_type msg;
				msg.cmd = MessageType::None;
				_async.queue(ep, msg);
				send(ep);
#ifdef SHORYU_ENABLE_LOG
				log << "[" << time_ms() - log_start << "] None --> " << zed_net_host_to_str(ep.host) << ":" << ep.port << "\n";
#endif
			}
			if (msg.cmd == MessageType::Ready)
			{
				send();
				m_ready_list.push_back(ep);
				_connection_cv.notify_all();
			}
			if (msg.cmd == MessageType::Chat)
			{
				if (m_chatmessage_handler)
					m_chatmessage_handler(msg.username, msg.lobby_message);
			}
		}

		bool join_handler(const zed_net_address_t& host_ep, int timeout)
		{
			_host_ep = host_ep;
			msec start_time = time_ms();
			std::unique_lock<std::mutex> lock(_connection_mutex);
			auto pred = [&](){
				return _current_state != MessageType::None;
			};

			do
			{
				if(_shutdown)
					return false;
				if(timeout > 0 && (time_ms() - start_time > timeout))
					return false;
				message_type msg(MessageType::Join);
				msg.username = _username.name;
				msg.host_ep = host_ep;
				msg.state = _state;
				if (!send(host_ep))
				{
					_async.queue(host_ep, msg);
					send(host_ep);
#ifdef SHORYU_ENABLE_LOG
				log << "[" << time_ms() - log_start << "] Out.Join\n";
#endif
				}
			}
			while(!_connection_cv.wait_for(lock, std::chrono::milliseconds(500), pred));

			if(_current_state == MessageType::Deny)
				return false;

			return true;
		}
		void join_recv_handler(const zed_net_address_t& ep, message_type& msg)
		{
#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] ";
			log << messageTypeNames[(int)msg.cmd] << std::setw(7) << msg.frame_id;
			log << " (" << _side << ") <--";
			log << " (" << (int)msg.side << ") " << zed_net_host_to_str(ep.host) << ":" << (int)ep.port;
			log << "\n";
#endif
			if(ep != _host_ep)
				return;
			std::unique_lock<std::mutex> lock(_connection_mutex);
			if(msg.cmd == MessageType::Info)
			{
				_side = msg.side;
                m_mcd_sync = msg.mcdsync;
				m_num_players = msg.num_players;
				std::srand(msg.rand_seed);
				_current_state = MessageType::Ready;
				if(!_state_check_handler(_state, msg.state))
					_current_state = MessageType::Deny;
				if(m_userlist_handler)
					m_userlist_handler(msg.usernames);
				_connection_cv.notify_all();
			}
			if(msg.cmd == MessageType::Deny)
			{
				_current_state = MessageType::Deny;
				_state_check_handler(_state, msg.state);
				_connection_cv.notify_all();
			}
			if(msg.cmd == MessageType::Delay)
			{
				delay(msg.delay);
				_connection_cv.notify_all();
			}
            if (msg.cmd == MessageType::MCDSync) {
				mcd_sync_begin = true;
                _connection_cv.notify_all();
            }
			if (msg.cmd == MessageType::Ping)
			{
				message_type msg;
				msg.cmd = MessageType::None;
				_async.queue(ep, msg);
				send(ep);
#ifdef SHORYU_ENABLE_LOG
				log << "[" << time_ms() - log_start << "] Ping  --> " << zed_net_host_to_str(ep.host) << ":" << ep.port << "\n";
#endif
			}
			if (msg.cmd == MessageType::Chat)
			{
				if (m_chatmessage_handler)
					m_chatmessage_handler(msg.username, msg.lobby_message);
			}
		}
		
		void recv_hdl(const zed_net_address_t& ep, message_type& msg)
		{
#ifdef SHORYU_ENABLE_LOG
			log << "[" << std::setw(12) << time_ms() - log_start << "] ";
			log << messageTypeNames[(int)msg.cmd] << std::setw(7) << msg.frame_id;
			log << " (" << _side << ") <--";
			log << " (" << (int)msg.side << ") " << zed_net_host_to_str(ep.host) << ":" << (int)ep.port;
			log << "\n";
#endif

			// clients ignore messages not from host
			if (!m_host && ep != _host_ep)
				return;

			//if(_sides.find(ep) != _sides.end())
			{
				int side = msg.side; //_sides[ep];

				// if we're server, echo to everyone else
				if (m_host && side != 0)
				{
					for (size_t i = 0; i < m_clientEndpoints.size(); i++)
					{
						if (i + 1 == side)
							continue;
						_async.queue(m_clientEndpoints[i], msg);
						send(m_clientEndpoints[i]);
					}
				}

				if(msg.cmd == MessageType::Frame)
				{
					std::unique_lock<std::mutex> lock(_mutex);
					_frame_table[side][msg.frame_id] = msg.frame;
					if(_first_received_frame < 0)
						_first_received_frame = msg.frame_id;
					else if(msg.frame_id < _first_received_frame)
						_first_received_frame = msg.frame_id;

					if(_last_received_frame < 0)
						_last_received_frame = msg.frame_id;
					else if(msg.frame_id > _last_received_frame)
						_last_received_frame = msg.frame_id;
					_frame_cond.notify_all();
				}
				if(msg.cmd == MessageType::Data)
				{
					std::unique_lock<std::mutex> lock(_mutex);
					_data_table[side][msg.frame_id] = msg.data;
					_data_cond.notify_all();
					if (m_host || side == 0)
						send(ep);
				}
				if(msg.cmd == MessageType::Delay)
				{
					std::unique_lock<std::mutex> lock(_mutex);
                    m_ready = true;
					delay(msg.delay);
					if (m_host || side == 0)
						send(ep);
				}
				if(msg.cmd == MessageType::EndSession)
				{
					std::unique_lock<std::mutex> lock(_mutex);
					_end_session_request = true;
					if (m_host || side == 0)
						send(ep);
				}
				if (msg.cmd == MessageType::Chat)
				{
					if (m_chatmessage_handler)
						m_chatmessage_handler(msg.username, msg.lobby_message);
				}
			}
		}
		void err_hdl(const std::error_code& error)
		{
			std::unique_lock<std::mutex> lock(_error_mutex);
			_last_error = error.message();
		}
	private:
		volatile int _delay;
		int64_t _frame;
		int64_t _data_index;
		bool m_host;
		int _side;
		bool _end_session_request;
		
		userinfo _username;
		typedef std::map<zed_net_address_t, userinfo> username_map;

		username_map _username_map;
		std::vector<zed_net_address_t> m_ready_list;
		bool m_ready;
        bool mcd_sync_begin;

		std::string _last_error;

		async_transport<message_type> _async;
		endpoint_container m_clientEndpoints;
		int m_num_players;
        bool m_mcd_sync;
		frame_table _frame_table;
		std::mutex _mutex;
		std::mutex _error_mutex;
		std::condition_variable _frame_cond;
		std::condition_variable _data_cond;
		data_table _data_table;

		std::function<const void(std::vector<userinfo>)> m_userlist_handler;
		std::unique_ptr<std::thread> m_ping_thread;
		bool m_ping_clients;
	};
}
