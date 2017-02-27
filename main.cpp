#include <iostream>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include "analyzer.h"

namespace po = boost::program_options;
namespace fs = boost::filesystem;

int main(int argc, char *argv[])
{
    bool verbose;
    po::options_description visible_options("Options");
    visible_options.add_options()
       ("help",                                                            "display this help")
       ("verbose,v",                 po::value(&verbose)->zero_tokens(),   "be verbose")
       ;

    po::options_description hidden_options("Hidden options");
    hidden_options.add_options()
       ("input",                     po::value<std::string>()->required(), "input file")
       ;

    po::options_description cmdline_options;
    cmdline_options.add(visible_options).add(hidden_options);

    po::positional_options_description p;
    p.add("input", 1);

    po::variables_map vm;
    try
    {
       po::store(po::command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);
       if (vm.count("help"))
       {
          std::cout << "Usage: " << argv[0] << " [options] file" << std::endl
                    << visible_options << std::endl
                    ;
          return EXIT_SUCCESS;
       }

       po::notify(vm);
    }
    catch (po::error const & e)
    {
       std::cerr << "Command line options storing failed:" << std::endl
                 << e.what() << std::endl
                 << std::endl
                 << "Try using --help option" << std::endl
                 ;
       return EXIT_FAILURE;
    }

    fs::path input_path(vm["input"].as<std::string>());
    analyzer_t analyzer;
    analyzer.analyze_file(input_path);

    return 0;
}
