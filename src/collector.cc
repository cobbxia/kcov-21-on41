#include <collector.hh>
#include <file-parser.hh>
#include <utils.hh>
#include <engine.hh>
#include <output-handler.hh>
#include <configuration.hh>
#include <filter.hh>
#include <signal.h>

#include <unordered_map>
#include <string>
#include <vector>

using namespace kcov;

class Collector :
		public ICollector,
		public IFileParser::ILineListener,
		public IEngine::IEventListener
{
public:
	Collector(IFileParser &fileParser, IEngine &engine, IFilter &filter) :
		m_fileParser(fileParser),
		m_engine(engine),
		m_exitCode(-1),
		m_filter(filter)
	{
		m_fileParser.registerLineListener(*this);
	}

	void registerListener(ICollector::IListener &listener)
	{
		m_listeners.push_back(&listener);
	}

	int run(const std::string &filename)
	{
		if (!m_engine.start(*this, filename)) {
			error("Can't start/attach to %s", filename.c_str());
			return -1;
		}

		IOutputHandler &output = IOutputHandler::getInstance();
		unsigned int outputInterval = IConfiguration::getInstance().getOutputInterval();

		// This will set all breakpoints
		m_fileParser.parse();
		m_engine.setupAllBreakpoints();

		uint64_t lastTimestamp = get_ms_timestamp();

		while (1) {
			bool shouldContinue = m_engine.continueExecution();

			uint64_t now = get_ms_timestamp();

			if (outputInterval != 0 &&
					now - lastTimestamp >= outputInterval) {
				output.produce();

				// Take a new timestamp since producing might take a long time
				lastTimestamp = get_ms_timestamp();
			}

			if (!shouldContinue)
				break;
		}

		return m_exitCode;
	}

	virtual void stop()
	{
	}

private:

	std::string eventToName(IEngine::Event ev)
	{
		switch (ev.type)
		{
		case ev_breakpoint:
			return fmt("breakpoint at 0x%llx", (unsigned long long)ev.addr);
		case ev_exit:
			return fmt("exit code %d", ev.data);
		case ev_signal:
		case ev_signal_exit:
		{
			if (ev.data == SIGABRT)
				return std::string("SIGABRT");
			if (ev.data == SIGSEGV)
				return std::string("SIGSEGV");
			if (ev.data == SIGILL)
				return std::string("SIGILL");
			if (ev.data == SIGTERM)
				return std::string("SIGTERM");
			if (ev.data == SIGINT)
				return std::string("SIGINT");
			if (ev.data == SIGBUS)
				return std::string("SIGBUS");
			if (ev.data == SIGFPE)
				return std::string("SIGFPE");

			return fmt("unknown signal %d", ev.data);
		}
		case ev_error:
			return std::string("error");
		default:
			break;
		}

		return std::string("unknown");
	}


	// From IEngine
	void onEvent(const IEngine::Event &ev)
	{
		switch (ev.type)
		{
		case ev_error:
			break;
		case ev_signal:
			break;
		case ev_signal_exit:
			kcov_debug(STATUS_MSG, "kcov: Process exited with signal %d (%s) at 0x%llx\n",
					ev.data, eventToName(ev).c_str(), (unsigned long long)ev.addr);

			if (ev.data == SIGILL)
				kcov_debug(STATUS_MSG,
						"\nkcov: Illegal instructions are sometimes caused by some GCC versions\n"
						"kcov: miscompiling C++ headers. If the problem is persistent, try running\n"
						"kcov: with --exclude-pattern=/usr/include. For more information, see\n"
						"kcov: http://github.com/SimonKagstrom/kcov/issues/18\n");

			break;

		case ev_exit_first_process:
			if (IConfiguration::getInstance().getExitFirstProcess()) {
				IConfiguration &conf = IConfiguration::getInstance();
				std::string fifoName = conf.getOutDirectory() + conf.getBinaryName() + "/done.fifo";

				std::string exitCode = fmt("%u", ev.data);

				write_file(exitCode.c_str(), exitCode.size(), "%s", fifoName.c_str());
			}
			m_exitCode = ev.data;
			break;
		case ev_exit:
			m_exitCode = ev.data;
			break;
		case ev_breakpoint:
			for (ListenerList_t::const_iterator it = m_listeners.begin();
					it != m_listeners.end();
					++it)
				(*it)->onAddress(ev.addr, 1);

			// Disable this breakpoint
			m_engine.clearBreakpoint(ev.data);

			break;

		default:
			panic("Unknown event %d", ev.type);
		}
	}


	// From IFileParser
	void onLine(const std::string &file, unsigned int lineNr, unsigned long addr)
	{
		if (!m_filter.runFilters(file))
		{
			return;
		}

		if (addr == 0)
			return;

		m_engine.registerBreakpoint(addr);
		m_addrs[addr]++;
	}

	typedef std::unordered_map<unsigned long, int> AddrMap_t;
	typedef std::vector<ICollector::IListener *> ListenerList_t;

	AddrMap_t m_addrs;
	IFileParser &m_fileParser;
	IEngine &m_engine;
	ListenerList_t m_listeners;
	int m_exitCode;

	IFilter &m_filter;
};

ICollector &ICollector::create(IFileParser &elf, IEngine &engine, IFilter &filter)
{
	return *new Collector(elf, engine, filter);
}
