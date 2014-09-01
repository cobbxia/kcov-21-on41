#include <file-parser.hh>
#include <engine.hh>
#include <configuration.hh>
#include <output-handler.hh>
#include <lineid.hh>
#include <utils.hh>

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include <list>
#include <unordered_map>
#include <vector>

#include <swap-endian.hh>

using namespace kcov;

extern std::vector<uint8_t> python_helper_data;


const uint64_t COVERAGE_MAGIC = 0x6d6574616c6c6775ULL; // "metallgut"

/* Should be 8-byte aligned */
struct coverage_data
{
	uint64_t magic;
	uint32_t size;
	uint32_t line;
	const char filename[];
};

class PythonEngine : public IEngine, public IFileParser
{
public:
	PythonEngine() :
		m_child(0),
		m_pipe(NULL),
		m_listener(NULL),
		m_currentAddress(1) // 0 is an invalid address
	{
		IEngineFactory::getInstance().registerEngine(*this);
		IParserManager::getInstance().registerParser(*this);
	}

	~PythonEngine()
	{
		kill(SIGTERM);
	}

	// From IEngine
	int registerBreakpoint(unsigned long addr)
	{
		// No breakpoints
		return 0;
	}

	void setupAllBreakpoints()
	{
	}

	bool clearBreakpoint(int id)
	{
		return true;
	}

	bool start(IEventListener &listener, const std::string &executable)
	{
		std::string kcov_python_pipe_path =
				IOutputHandler::getInstance().getOutDirectory() + "kcov-python.pipe";
		std::string kcov_python_path =
				IOutputHandler::getInstance().getBaseDirectory() + "python-helper.py";

		if (write_file(python_helper_data.data(), python_helper_data.size(),
				"%s", kcov_python_path.c_str()) < 0) {
				error("Can't write python helper at %s", kcov_python_path.c_str());

				return false;
		}

		m_listener = &listener;

		std::string kcov_python_env = "KCOV_PYTHON_PIPE_PATH=" + kcov_python_pipe_path;
		unlink(kcov_python_pipe_path.c_str());
		mkfifo(kcov_python_pipe_path.c_str(), 0600);

		char *envString = (char *)xmalloc(kcov_python_env.size() + 1);
		strcpy(envString, kcov_python_env.c_str());

		putenv(envString);

		/* Launch the python helper */
		m_child = fork();
		if (m_child == 0) {
			IConfiguration &conf = IConfiguration::getInstance();
			const char **argv = conf.getArgv();
			unsigned int argc = conf.getArgc();

			std::string s = fmt("%s %s ",
					conf.getPythonCommand().c_str(),
					kcov_python_path.c_str());
			for (unsigned int i = 0; i < argc; i++)
				s += std::string(argv[i]) + " ";

			int res;

			res = system(s.c_str());
			panic_if (res < 0,
					"Can't execute python helper");

			exit(WEXITSTATUS(res));
		} else if (m_child < 0) {
			perror("fork");

			return false;
		}
		m_pipe = fopen(kcov_python_pipe_path.c_str(), "r");
		panic_if (!m_pipe,
				"Can't open python pipe %s", kcov_python_pipe_path.c_str());

		return true;
	}

	bool checkEvents()
	{
		uint8_t buf[8192];
		struct coverage_data *p;

		p = readCoverageDatum(buf, sizeof(buf));

		if (!p) {
			reportEvent(ev_error, -1);

			return false;
		}

		if (!m_reportedFiles[p->filename]) {
			m_reportedFiles[p->filename] = true;

			for (FileListenerList_t::const_iterator it = m_fileListeners.begin();
					it != m_fileListeners.end();
					++it)
				(*it)->onFile(p->filename, IFileParser::FLG_NONE);

			parseFile(p->filename);
		}

		if (m_listener) {
			uint64_t address = 0;
			Event ev;

			LineIdToAddressMap_t::const_iterator it = m_lineIdToAddress.find(getLineId(p->filename, p->line));
			if (it != m_lineIdToAddress.end())
				address = it->second;

			ev.type = ev_breakpoint;
			ev.addr = address;
			ev.data = 1;

			m_listener->onEvent(ev);
		}

		return true;
	}

	bool continueExecution()
	{
		if (checkEvents())
			return true;


		// Otherwise wait for child
		int status;
		int rv;

		rv = waitpid(m_child, &status, WNOHANG);
		if (rv != m_child)
			return true;

		if (WIFEXITED(status)) {
			reportEvent(ev_exit_first_process, WEXITSTATUS(status));
		} else {
			warning("Other status: 0x%x\n", status);
			reportEvent(ev_error, -1);
		}

		return false;
	}

	void kill(int signal)
	{
		if (m_child == 0)
			return;

		::kill(m_child, signal);
	}

	unsigned int matchFile(const std::string &filename, uint8_t *data, size_t dataSize)
	{
		return matchParser(filename, data, dataSize);
	}


	// From IFileParser
	bool addFile(const std::string &filename, struct phdr_data_entry *phdr_data)
	{
		return true;
	}

	void registerLineListener(ILineListener &listener)
	{
		m_lineListeners.push_back(&listener);
	}

	void registerFileListener(IFileListener &listener)
	{
		m_fileListeners.push_back(&listener);
	}

	bool parse()
	{
		return true;
	}

	uint64_t getChecksum()
	{
		return 0;
	}

	unsigned int matchParser(const std::string &filename, uint8_t *data, size_t dataSize)
	{
		std::string s((const char *)data, 80);

		if (filename.substr(filename.size() - 3, filename.size()) == ".py")
			return 200;

		if (s.find("python") != std::string::npos)
			return 100;

		return match_none;
	}

	class Ctor
	{
	public:
		Ctor()
		{
			new PythonEngine();
		}
	};

private:
	void reportEvent(enum event_type type, int data = -1, uint64_t address = 0)
	{
		if (!m_listener)
			return;

		m_listener->onEvent(Event(type, data, address));
	}

	void unmarshalCoverageData(struct coverage_data *p)
	{
		p->magic = be_to_host<uint64_t>(p->magic);
		p->size = be_to_host<uint32_t>(p->size);
		p->line = be_to_host<uint32_t>(p->line);
	}

	// Sweep through lines in a file to determine what is valid code
	void parseFile(const std::string &filename)
	{
		if (!m_listener)
			return;

		size_t sz;
		char *p = (char *)read_file(&sz, "%s", filename.c_str());

		// Can't handle this file
		if (!p)
			return;
		std::string fileData(p, sz);

		// Compute crc32 for this file
		uint32_t crc = crc32((const uint8_t *)p, sz);

		free((void*)p);

		const std::vector<std::string> &stringList = split_string(fileData, "\n");
		unsigned int lineNo = 0;
		enum { start, multiline_active } state = start;
		bool multiLineStartLine = false;

		for (std::vector<std::string>::const_iterator it = stringList.begin();
				it != stringList.end();
				++it) {
			const std::string &s = trim_string(*it);

			lineNo++;
			// Empty line, ignore
			if (s == "")
				continue;

			// Non-empty, but comment
			if (s[0] == '#')
				continue;

			// Dict endings generate no code
			if (s == "}" || s == "},")
				continue;

			// Ditto for list ends
			if (s == "]" || s == "],")
				continue;

			// ... And starts
			if (s == "[")
				continue;

			// else: statements are nops
			if (s.find("else:") != std::string::npos)
				continue;

			// Single-line python strings (e.g., 'description of the function')
			if (isPythonString(s))
				continue;

			size_t idx = multilineIdx(s);

			switch (state)
			{
			case start:
				if (idx != std::string::npos) {
					kcov_debug(PTRACE_MSG, "python multiline ON  %3d: %s\n", lineNo, s.c_str());

					std::string s2 = s.substr(idx + 3, std::string::npos);

					if (multilineIdx(s2) == std::string::npos)
						state = multiline_active;

					// E.g., a = '''yadayada [...]'''
					if (idx > 0)
						multiLineStartLine = true;

					// Don't report this line
					continue;
				}
				break;
			case multiline_active:
				if (idx != std::string::npos) {
					kcov_debug(PTRACE_MSG, "python multiline OFF %3d: %s\n", lineNo, s.c_str());
					state = start;

					// The last line of a multi-line string will get reported by the
					// python helper, so add this as a line if there was an assignment
					// above
					if (multiLineStartLine) {
						fileLineFound(crc, filename, lineNo);
						multiLineStartLine = false;
					}
				}
				continue; // Don't report this line
			default:
				panic("Unknown state %u", state);
				break;
			}

			fileLineFound(crc, filename, lineNo);
		}
	}

	void fileLineFound(uint32_t crc, const std::string &filename, unsigned int lineNo)
	{
		size_t id = getLineId(filename, lineNo);
		uint64_t address = m_currentAddress ^ crc;

		m_lineIdToAddress[id] = address;

		for (LineListenerList_t::const_iterator lit = m_lineListeners.begin();
				lit != m_lineListeners.end();
				++lit)
			(*lit)->onLine(get_real_path(filename).c_str(), lineNo, address);

		m_currentAddress++;
	}

	size_t multilineIdx(const std::string &s)
	{
		size_t idx = s.find("'''");

		if (idx == std::string::npos)
			idx = s.find("\"\"\"");

		return idx;
	}

	bool isStringDelimiter(char c)
	{
		return c == '\'' || c == '"';
	}

	bool isPythonString(const std::string &s)
	{
		if (s.size() < 2)
			return false;

		char first = s[0];
		char last = s[s.size() - 1];

		if (!isStringDelimiter(first))
			return false;

		// Can be multi-line string, handled elsewhere
		if (!isStringDelimiter(last))
			return false;

		unsigned i;
		for (i = 0; i < s.size(); i++) {
			if (!isStringDelimiter(s[i]))
				break;
		}
		// Only string characters
		if (i == s.size())
			return false;

		return true;
	}


	struct coverage_data *readCoverageDatum(uint8_t *buf, size_t totalSize)
	{
		struct coverage_data *p = (struct coverage_data *)buf;
		ssize_t rv;

		if (feof(m_pipe))
			return NULL; // Not an error

		// No data?
		if (!file_readable(m_pipe, 100))
			return NULL;

		rv = fread(buf, 1, sizeof(struct coverage_data), m_pipe);
		if (rv == 0)
			return NULL; // Not an error

		if (rv < (int)sizeof(struct coverage_data)) {
			error("Read too little %zd", rv);

			return NULL;
		}
		unmarshalCoverageData(p);

		bool valid = p->magic == COVERAGE_MAGIC && p->size < totalSize;

		kcov_debug(PTRACE_MSG, "datum: 0x%16llx, size %u, line %u (%svalid)\n",
				(unsigned long long)p->magic, (unsigned int)p->size,
				(unsigned int)p->line, valid ? "" : "in");

		if (!valid) {
			uint32_t *p32 = (uint32_t *)buf;

			error("Data magic wrong or size too large: %08x %08x %08x %08x\n",
					p32[0], p32[1], p32[2], p32[3]);

			return NULL;
		}

		size_t remainder = p->size - sizeof(struct coverage_data);
		rv = fread(buf + sizeof(struct coverage_data), 1, remainder, m_pipe);
		if (rv < (ssize_t)remainder) {
			error("Read too little %zd vs %zu", rv, remainder);

			return NULL;
		}

		return p;
	}

	typedef std::vector<ILineListener *> LineListenerList_t;
	typedef std::vector<IFileListener *> FileListenerList_t;
	typedef std::unordered_map<std::string, bool> ReportedFileMap_t;
	typedef std::unordered_map<size_t, uint64_t> LineIdToAddressMap_t;

	pid_t m_child;
	FILE *m_pipe;

	LineListenerList_t m_lineListeners;
	FileListenerList_t m_fileListeners;
	ReportedFileMap_t m_reportedFiles;
	LineIdToAddressMap_t m_lineIdToAddress;

	IEventListener *m_listener;
	uint64_t m_currentAddress;
};

static PythonEngine::Ctor g_pythonEngine;
