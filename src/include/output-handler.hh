#pragma once

#include <string>

namespace kcov
{
	class IWriter;
	class IReporter;
	class IFileParser;

	class IOutputHandler
	{
	public:
		virtual ~IOutputHandler() {}

		virtual void registerWriter(IWriter &writer) = 0;

		virtual void start() = 0;

		virtual void produce() = 0;


		virtual const std::string &getBaseDirectory() = 0;

		virtual const std::string &getOutDirectory() = 0;

		static IOutputHandler &create(IFileParser &parser, IReporter &reporter);

		static IOutputHandler &getInstance();
	};
}
