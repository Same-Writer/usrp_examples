#include <uhd/convert.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/utils/thread.hpp>
#include <stdint.h>
#include <stdlib.h>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/tokenizer.hpp>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace po = boost::program_options;

int UHD_SAFE_MAIN(int argc, char* argv[])
{
    // variables to be set by po
    std::string args;
    uint32_t gpio_line;
    //uint32_t ddr = strtoul("0x0".c_str(), NULL, 0);
    //uint32_t out = strtoul("0x0".c_str(), NULL, 0);


    // setup the program options
    po::options_description desc("Allowed options");
    // clang-format off
    desc.add_options()
        ("help", "help message")
        ("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
        ("gpio line", po::value<uint32_t>(&gpio_line)->default_value(gpio_line), "")
      ;

    // create a usrp device
    std::cout << std::endl;
    std::cout << boost::format("Creating the usrp device with: %s...") % args
              << std::endl;
    uhd::usrp::multi_usrp::sptr usrp = uhd::usrp::multi_usrp::make(args);
    std::cout << boost::format("Using Device: %s") % usrp->get_pp_string() << std::endl;
    
    /********************************************/
    /*********** begin gpio operations **********/
    /********************************************/

    //gpio_line = 1 << 5; // format is: 1 << desired_gpio_pin
    gpio_line = 0xFF; // format is: 1 << desired_gpio_pin
    

    // reset usrp time to 0.0s
    usrp->set_time_source("internal");
    usrp->set_time_next_pps(uhd::time_spec_t(0.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    // set gpio up for output, and put the relevant pin(s) low to start
    usrp->set_gpio_attr("FP0", "DDR", "OUT", gpio_line, 0);
    usrp->set_gpio_attr("FP0", "CTRL", "GPIO", gpio_line, 0);
    usrp->set_gpio_attr("FP0", "OUT", "HIGH", gpio_line, 0);

    std::cout << boost::format("GPIO OUT Readback: %s") % usrp->get_gpio_attr("FP0", "OUT", 0) << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));

    //usrp->clear_command_time();
    //usrp->set_command_time(usrp->get_time_now() + uhd::time_spec_t(2.0));

    //usrp->set_gpio_attr("FP0", "OUT", "HIGH", gpio_line, 0);

    //usrp->clear_command_time();

    /********************************************/
    /*********** quit gpio operations ***********/
    /********************************************/

    // finished
    std::cout << std::endl << "Done!" << std::endl << std::endl;

    return EXIT_SUCCESS;
}
