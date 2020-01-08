/*
 *
 *
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */

#include <ace/Get_Opt.h>

#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/Service_Participant.h>

#include "dds/DCPS/StaticIncludes.h"

#ifdef ACE_AS_STATIC_LIBS
# ifndef OPENDDS_SAFETY_PROFILE
#include <dds/DCPS/transport/udp/Udp.h>
#include <dds/DCPS/transport/multicast/Multicast.h>
#include <dds/DCPS/RTPS/RtpsDiscovery.h>
#include <dds/DCPS/transport/shmem/Shmem.h>
#  ifdef OPENDDS_SECURITY
#  include "dds/DCPS/security/BuiltInPlugins.h"
#  endif
# endif
#include <dds/DCPS/transport/rtps_udp/RtpsUdp.h>
#endif

#include "MessengerTypeSupportImpl.h"
#include "Writer.h"

#include <dds/DCPS/BuiltInTopicUtils.h>
#include "ParticipantLocationListenerImpl.h"

#include <iostream>

bool no_ice = false;

int parse_args (int argc, ACE_TCHAR *argv[])
{
  ACE_Get_Opt get_opts (argc, argv, ACE_TEXT ("n"));
  int c;

  while ((c = get_opts ()) != -1)
    switch (c)
      {
      case 'n':
        no_ice = true;
        break;
      case '?':
      default:
        ACE_ERROR_RETURN ((LM_ERROR,
                           "usage:  %s "
                           "-n do not check for ICE connections"
                           "\n",
                           argv [0]),
                          -1);
      }
  // Indicates successful parsing of the command line
  return 0;
}

int ACE_TMAIN(int argc, ACE_TCHAR *argv[])
{
  DDS::DomainParticipantFactory_var dpf;
  DDS::DomainParticipant_var participant;

  int status = EXIT_FAILURE;

  try {
    std::cout << "Starting publisher" << std::endl;
    {
      // Initialize DomainParticipantFactory
      dpf = TheParticipantFactoryWithArgs(argc, argv);

      if( parse_args(argc, argv) != 0)
        return 1;

      DDS::DomainParticipantQos part_qos;
      dpf->get_default_participant_qos(part_qos);


      // Create DomainParticipant
      participant = dpf->create_participant(42,
                                part_qos,
                                DDS::DomainParticipantListener::_nil(),
                                OpenDDS::DCPS::DEFAULT_STATUS_MASK);

      if (CORBA::is_nil(participant.in())) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l: main()")
                          ACE_TEXT(" ERROR: create_participant failed!\n")),
                         EXIT_FAILURE);
      }

      // Register TypeSupport (Messenger::Message)
      Messenger::MessageTypeSupport_var mts =
        new Messenger::MessageTypeSupportImpl();

      if (mts->register_type(participant.in(), "") != DDS::RETCODE_OK) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l: main()")
                          ACE_TEXT(" ERROR: register_type failed!\n")),
                         EXIT_FAILURE);
      }

      // Create Topic
      CORBA::String_var type_name = mts->get_type_name();
      DDS::Topic_var topic =
        participant->create_topic("Movie Discussion List",
                                  type_name.in(),
                                  TOPIC_QOS_DEFAULT,
                                  DDS::TopicListener::_nil(),
                                  OpenDDS::DCPS::DEFAULT_STATUS_MASK);

      if (CORBA::is_nil(topic.in())) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l: main()")
                          ACE_TEXT(" ERROR: create_topic failed!\n")),
                         EXIT_FAILURE);
      }

      // Create Publisher
      DDS::PublisherQos publisher_qos;
      participant->get_default_publisher_qos(publisher_qos);
      publisher_qos.partition.name.length(1);
      publisher_qos.partition.name[0] = "OCI";

      DDS::Publisher_var pub =
        participant->create_publisher(publisher_qos,
                                      DDS::PublisherListener::_nil(),
                                      OpenDDS::DCPS::DEFAULT_STATUS_MASK);

      if (CORBA::is_nil(pub.in())) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l: main()")
                          ACE_TEXT(" ERROR: create_publisher failed!\n")),
                         EXIT_FAILURE);
      }

      DDS::DataWriterQos qos;
      pub->get_default_datawriter_qos(qos);
      std::cout << "Reliable DataWriter" << std::endl;
      qos.history.kind = DDS::KEEP_ALL_HISTORY_QOS;
      qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;

      // Create DataWriter
      DDS::DataWriter_var dw =
        pub->create_datawriter(topic.in(),
                               qos,
                               DDS::DataWriterListener::_nil(),
                               OpenDDS::DCPS::DEFAULT_STATUS_MASK);

      if (CORBA::is_nil(dw.in())) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l: main()")
                          ACE_TEXT(" ERROR: create_datawriter failed!\n")),
                         EXIT_FAILURE);
      }

      // Get the Built-In Subscriber for Built-In Topics
      DDS::Subscriber_var bit_subscriber = participant->get_builtin_subscriber();

      DDS::DataReader_var pub_loc_dr = bit_subscriber->lookup_datareader(OpenDDS::DCPS::BUILT_IN_PARTICIPANT_LOCATION_TOPIC);
      if (0 == pub_loc_dr) {
        std::cerr << "Could not get " << OpenDDS::DCPS::BUILT_IN_PARTICIPANT_LOCATION_TOPIC
                  << " DataReader." << std::endl;
        ACE_OS::exit(EXIT_FAILURE);
      }

      unsigned long locations = 0;

      DDS::DataReaderListener_var pub_loc_listener =
        new ParticipantLocationListenerImpl("Publisher", locations);

      CORBA::Long retcode =
        pub_loc_dr->set_listener(pub_loc_listener,
                                OpenDDS::DCPS::DEFAULT_STATUS_MASK);
      if (retcode != DDS::RETCODE_OK) {
        std::cerr << "set_listener for " << OpenDDS::DCPS::BUILT_IN_PARTICIPANT_LOCATION_TOPIC << " failed." << std::endl;
        ACE_OS::exit(EXIT_FAILURE);
      }

      // Start writing threads
      std::cout << "Creating Writer" << std::endl;
      Writer* writer = new Writer(dw.in());
      std::cout << "Starting Writer" << std::endl;
      writer->start();

      while (!writer->is_finished()) {
        ACE_Time_Value small_time(0, 250000);
        ACE_OS::sleep(small_time);
      }

      std::cout << "Writer finished " << std::endl;
      writer->end();

      std::cout << "Writer wait for ACKS" << std::endl;

      DDS::Duration_t timeout = { DDS::DURATION_INFINITE_SEC, DDS::DURATION_INFINITE_NSEC };
      dw->wait_for_acknowledgments(timeout);

      // delay
      ACE_OS::sleep(2);

      std::cerr << "deleting DW" << std::endl;
      delete writer;

      // check that all locations received
      unsigned long local_relay = OpenDDS::DCPS::LOCATION_LOCAL | OpenDDS::DCPS::LOCATION_RELAY;
      unsigned long local_relay_ice = local_relay | OpenDDS::DCPS::LOCATION_ICE;

      // check for local and relay first || check for local, relay and ice
      if (no_ice && locations == local_relay) {
        status = EXIT_SUCCESS;
        std::cerr << "Publisher success. Found locations LOCAL and RELAY." << std::endl;
      }
      else if (!no_ice && locations == local_relay_ice) {
        status = EXIT_SUCCESS;
        std::cerr << "Publisher success. Found locations LOCAL, RELAY and ICE." << std::endl;
      }
      else {
        std::cerr << "Error in publisher: One or more locations missing. Location mask "
                  << locations << " != " << local_relay << " (local & relay) and  "
                  << locations << " != " << local_relay_ice <<  " (local, relay & ice)." << std::endl;
        status = EXIT_FAILURE;
      }
    }

    // Clean-up!
    std::cerr << "publisher deleting contained entities" << std::endl;
    participant->delete_contained_entities();
    std::cerr << "publisher deleting participant" << std::endl;
    dpf->delete_participant(participant.in());
    std::cerr << "publisher shutdown" << std::endl;
    TheServiceParticipant->shutdown();

  } catch (const CORBA::Exception& e) {
    e._tao_print_exception("Exception caught in main():");
    status = EXIT_FAILURE;
  }

  return status;
}
