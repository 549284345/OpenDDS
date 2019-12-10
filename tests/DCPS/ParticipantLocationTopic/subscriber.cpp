/*
 *
 *
 * Distributed under the OpenDDS License.
 * See: http://www.opendds.org/license.html
 */


#include <dds/DdsDcpsInfrastructureC.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/SubscriberImpl.h>
#include <dds/DCPS/WaitSet.h>

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

#include <dds/DCPS/transport/framework/TransportRegistry.h>
#include <dds/DCPS/transport/framework/TransportConfig.h>
#include <dds/DCPS/transport/framework/TransportInst.h>

#include "DataReaderListener.h"
#include "MessengerTypeSupportImpl.h"

#include <dds/DCPS/BuiltInTopicUtils.h>
#include "ParticipantLocationListenerImpl.h"

#include <iostream>

bool reliable = false;
bool wait_for_acks = false;

int
ACE_TMAIN(int argc, ACE_TCHAR *argv[])
{
  int status = EXIT_FAILURE;

  try {
    // Initialize DomainParticipantFactory
    DDS::DomainParticipantFactory_var dpf =
      TheParticipantFactoryWithArgs(argc, argv);

    DDS::DomainParticipantQos part_qos;
    dpf->get_default_participant_qos(part_qos);

    // Create DomainParticipant
    DDS::DomainParticipant_var participant =
      dpf->create_participant(42,
                              part_qos,
                              DDS::DomainParticipantListener::_nil(),
                              OpenDDS::DCPS::DEFAULT_STATUS_MASK);

    if (CORBA::is_nil(participant.in())) {
      ACE_ERROR_RETURN((LM_ERROR,
                        ACE_TEXT("%N:%l main()")
                        ACE_TEXT(" ERROR: create_participant() failed!\n")), EXIT_FAILURE);
    }

    // Register Type (Messenger::Message)
    Messenger::MessageTypeSupport_var ts =
      new Messenger::MessageTypeSupportImpl();

    if (ts->register_type(participant.in(), "") != DDS::RETCODE_OK) {
      ACE_ERROR_RETURN((LM_ERROR,
                        ACE_TEXT("%N:%l main()")
                        ACE_TEXT(" ERROR: register_type() failed!\n")), EXIT_FAILURE);
    }

    // Create Topic (Movie Discussion List)
    CORBA::String_var type_name = ts->get_type_name();
    DDS::Topic_var topic =
      participant->create_topic("Movie Discussion List",
                                type_name.in(),
                                TOPIC_QOS_DEFAULT,
                                DDS::TopicListener::_nil(),
                                OpenDDS::DCPS::DEFAULT_STATUS_MASK);

    if (CORBA::is_nil(topic.in())) {
      ACE_ERROR_RETURN((LM_ERROR,
                        ACE_TEXT("%N:%l main()")
                        ACE_TEXT(" ERROR: create_topic() failed!\n")), EXIT_FAILURE);
    }

    // Create Subscriber
    DDS::SubscriberQos subscriber_qos;
    participant->get_default_subscriber_qos(subscriber_qos);
    subscriber_qos.partition.name.length(1);
    subscriber_qos.partition.name[0] = "?CI";

    DDS::Subscriber_var sub =
      participant->create_subscriber(subscriber_qos,
                                     DDS::SubscriberListener::_nil(),
                                     OpenDDS::DCPS::DEFAULT_STATUS_MASK);

    if (CORBA::is_nil(sub.in())) {
      ACE_ERROR_RETURN((LM_ERROR,
                        ACE_TEXT("%N:%l main()")
                        ACE_TEXT(" ERROR: create_subscriber() failed!\n")), EXIT_FAILURE);
    }

    // Create DataReader
    DataReaderListenerImpl* const listener_servant = new DataReaderListenerImpl;
    DDS::DataReaderListener_var listener(listener_servant);

    DDS::DataReaderQos dr_qos;
    sub->get_default_datareader_qos(dr_qos);
    std::cout << "Reliable DataReader" << std::endl;
    dr_qos.reliability.kind = DDS::RELIABLE_RELIABILITY_QOS;

    DDS::DataReader_var reader =
      sub->create_datareader(topic.in(),
                             dr_qos,
                             listener.in(),
                             OpenDDS::DCPS::DEFAULT_STATUS_MASK);

    if (CORBA::is_nil(reader.in())) {
      ACE_ERROR_RETURN((LM_ERROR,
                        ACE_TEXT("%N:%l main()")
                        ACE_TEXT(" ERROR: create_datareader() failed!\n")), EXIT_FAILURE);
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
      new ParticipantLocationListenerImpl("Subscriber", locations);

    CORBA::Long retcode =
      pub_loc_dr->set_listener(pub_loc_listener,
                               OpenDDS::DCPS::DEFAULT_STATUS_MASK);
    if (retcode != DDS::RETCODE_OK) {
      std::cerr << "set_listener for " << OpenDDS::DCPS::BUILT_IN_PARTICIPANT_LOCATION_TOPIC << " failed." << std::endl;
      ACE_OS::exit(EXIT_FAILURE);
    }

    // Block until Publisher completes
    DDS::StatusCondition_var condition = reader->get_statuscondition();
    condition->set_enabled_statuses(DDS::SUBSCRIPTION_MATCHED_STATUS);

    DDS::WaitSet_var ws = new DDS::WaitSet;
    ws->attach_condition(condition);

    DDS::Duration_t timeout =
      { DDS::DURATION_INFINITE_SEC, DDS::DURATION_INFINITE_NSEC };

    DDS::ConditionSeq conditions;
    DDS::SubscriptionMatchedStatus matches = { 0, 0, 0, 0, 0 };

    while (true) {
      if (reader->get_subscription_matched_status(matches) != DDS::RETCODE_OK) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l main()")
                          ACE_TEXT(" ERROR: get_subscription_matched_status() failed!\n")), -EXIT_FAILURE);
      }
      if (matches.current_count == 0 && matches.total_count > 0) {
        break;
      }
      if (ws->wait(conditions, timeout) != DDS::RETCODE_OK) {
        ACE_ERROR_RETURN((LM_ERROR,
                          ACE_TEXT("%N:%l main()")
                          ACE_TEXT(" ERROR: wait() failed!\n")), EXIT_FAILURE);
      }
    }

    status = listener_servant->is_valid() ? EXIT_SUCCESS : EXIT_FAILURE;

    ws->detach_condition(condition);

    // delay to ensure location BIT received
    ACE_OS::sleep(2);

    // check that all locations received
    unsigned long all = OpenDDS::DCPS::LOCATION_LOCAL |
#ifdef OPENDDS_SECURITY
                        OpenDDS::DCPS::LOCATION_ICE |
#endif
                        OpenDDS::DCPS::LOCATION_RELAY;

    if (!status && locations == all) {
      status = EXIT_SUCCESS;
    }
    else {
      std::cerr << "Error in subscriber: One or more locations missing. Location mask " << locations << " != " << all <<  "." << std::endl;
      status = EXIT_FAILURE;
    }

    // Clean-up!
    std::cerr << "subscriber deleting contained entities" << std::endl;
    participant->delete_contained_entities();
    std::cerr << "subscriber deleting participant" << std::endl;
    dpf->delete_participant(participant.in());
    std::cerr << "subscriber shutdown" << std::endl;
    TheServiceParticipant->shutdown();

  } catch (const CORBA::Exception& e) {
    e._tao_print_exception("Exception caught in main():");
    status = EXIT_FAILURE;
  }

  return status;
}
