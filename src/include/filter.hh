#pragma once

#include <string>

namespace kcov
{
	class IFilter
	{
	public:
		class Handler
		{
		public:
			virtual bool includeFile(const std::string &file) = 0;
		};

		virtual bool runFilters(const std::string &file) = 0;


		static IFilter &create();

		static IFilter &createDummy();


		virtual ~IFilter() {}
	};
}
