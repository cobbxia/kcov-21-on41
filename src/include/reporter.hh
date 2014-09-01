#pragma once

#include <string>

#include <stddef.h>

namespace kcov
{
	class IFileParser;
	class ICollector;
	class IFilter;

	class IReporter
	{
	public:
		class LineExecutionCount
		{
		public:
			LineExecutionCount(unsigned int hits, unsigned int possibleHits) :
				m_hits(hits), m_possibleHits(possibleHits)
			{
			}

			unsigned int m_hits;
			unsigned int m_possibleHits;
		};

		class ExecutionSummary
		{
		public:
			ExecutionSummary() : m_lines(0), m_executedLines(0), m_includeInTotals(true)
			{
			}

			ExecutionSummary(unsigned int lines, unsigned int executedLines) :
				m_lines(lines), m_executedLines(executedLines), m_includeInTotals(true)
			{
			}

			unsigned int m_lines;
			unsigned int m_executedLines;
			unsigned int m_includeInTotals;
		};

		virtual ~IReporter() {}

		/**
		 * Return if a file path should be included in the output.
		 *
		 * @param file the file path to check
		 *
		 * @return true if the file should be included in the output
		 */
		virtual bool fileIsIncluded(const std::string &file) = 0;

		virtual bool lineIsCode(const std::string &file, unsigned int lineNr) = 0;

		virtual LineExecutionCount getLineExecutionCount(const std::string &file, unsigned int lineNr) = 0;

		virtual ExecutionSummary getExecutionSummary() = 0;

		virtual void *marshal(size_t *szOut) = 0;

		virtual bool unMarshal(void *data, size_t sz) = 0;

		static IReporter &create(IFileParser &elf, ICollector &collector, IFilter &filter);
	};
}
