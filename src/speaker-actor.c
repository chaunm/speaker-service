/*
 * speaker-actor.c
 *
 *  Created on: Sep 23, 2016
 *      Author: ChauNM
 */

#include "speaker-service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include "Actor/actor.h"
#include "lib/jansson/jansson.h"
#include "universal/typesdef.h"
#include "universal/universal.h"

static PACTOR speakerActor = NULL;

static void SpeakerActorOnPlayRequest(PVOID pParam)
{
	FILE* mediaFile;
	char* message = (char*)pParam;
	char **znpSplitMessage;
	char command[255];
	if (speakerActor == NULL) return;
	BOOL result;
	json_t* payloadJson = NULL;
	json_t* paramsJson = NULL;
	json_t* songJson = NULL;
	json_t* repeatTimeJson = NULL;
	json_t* responseJson = NULL;
	json_t* statusJson = NULL;
	json_t* errorJson = NULL;
	char* fileName;
	PACTORHEADER header;
	char* responseTopic;
	char* responseMessage;
	BYTE repeaTime;
	znpSplitMessage = ActorSplitMessage(message);
	if (znpSplitMessage == NULL)
		return;
	// parse header to get origin of message
	header = ActorParseHeader(znpSplitMessage[0]);
	if (header == NULL)
	{
		ActorFreeSplitMessage(znpSplitMessage);
		return;
	}
	//parse payload
	payloadJson = json_loads(znpSplitMessage[1], JSON_DECODE_ANY, NULL);
	if (payloadJson == NULL)
	{
		ActorFreeSplitMessage(znpSplitMessage);
		ActorFreeHeaderStruct(header);
		return;
	}
	paramsJson = json_object_get(payloadJson, "params");
	if (paramsJson == NULL)
	{
		json_decref(payloadJson);
		ActorFreeSplitMessage(znpSplitMessage);
		ActorFreeHeaderStruct(header);
		return;
	}
	repeatTimeJson = json_object_get(paramsJson, "repeatTime");
	if (repeatTimeJson == NULL)
		repeaTime = 1;
	else
		repeaTime = json_integer_value(repeatTimeJson);
	songJson = json_object_get(paramsJson, "song");
	if (songJson == NULL)
		result = 1;
	else
	{
		fileName = StrDup(json_string_value(songJson));
		mediaFile = fopen(fileName, "r");
		if (mediaFile == NULL)
			result = 1;
		else
		{
			fclose(mediaFile);
			if (strstr(fileName, ".mp3") == NULL)
				result = 2;
			else
			{
				sprintf(command, "mpg321 \"%s\" --loop %d", json_string_value(songJson), repeaTime);
				system(command);
				result = 0;
			}
		}
		free(fileName);
	}
	json_decref(repeatTimeJson);
	json_decref(songJson);
	json_decref(paramsJson);
	json_decref(payloadJson);

	//make response package
	responseJson = json_object();
	statusJson = json_object();
	json_t* requestJson = json_loads(znpSplitMessage[1], JSON_DECODE_ANY, NULL);
	json_object_set(responseJson, "request", requestJson);
	json_decref(requestJson);
	json_t* resultJson;
	switch (result)
	{
	case 0:
		resultJson = json_string("status.success");
		errorJson = json_string("error.none");
		break;
	case 1:
		resultJson = json_string("status.failure");
		errorJson = json_string("error.file_not_found");
		break;
	case 2:
		resultJson = json_string("status.failure");
		errorJson = json_string("error.not_mp3_file");
		break;
	default:
		break;
	}
	json_object_set(statusJson, "status", resultJson);
	json_object_set(statusJson, "error", errorJson);
	json_decref(resultJson);
	json_decref(errorJson);
	json_object_set(responseJson, "response", statusJson);
	json_decref(statusJson);
	responseMessage = json_dumps(responseJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	responseTopic = ActorMakeTopicName(header->origin, "/:response");
	ActorFreeHeaderStruct(header);
	json_decref(responseJson);
	ActorFreeSplitMessage(znpSplitMessage);
	ActorSend(speakerActor, responseTopic, responseMessage, NULL, FALSE);
	free(responseMessage);
	free(responseTopic);
}

static void SpeakerActorOnStopRequest(PVOID pParam)
{
	char* message = (char*)pParam;
	char **znpSplitMessage;
	if (speakerActor == NULL) return;
	json_t* responseJson = NULL;
	json_t* statusJson = NULL;
	PACTORHEADER header;
	char* responseTopic;
	char* responseMessage;
	znpSplitMessage = ActorSplitMessage(message);
	if (znpSplitMessage == NULL)
		return;
	// parse header to get origin of message
	header = ActorParseHeader(znpSplitMessage[0]);
	if (header == NULL)
	{
		ActorFreeSplitMessage(znpSplitMessage);
		return;
	}
	//
	system("killall mpg321");
	//make response package
	responseJson = json_object();
	statusJson = json_object();
	json_t* requestJson = json_loads(znpSplitMessage[1], JSON_DECODE_ANY, NULL);
	json_object_set(responseJson, "request", requestJson);
	json_decref(requestJson);
	json_t* resultJson;
	resultJson = json_string("status.success");
	json_object_set(statusJson, "status", resultJson);
	json_decref(resultJson);
	json_object_set(responseJson, "response", statusJson);
	json_decref(statusJson);
	responseMessage = json_dumps(responseJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	responseTopic = ActorMakeTopicName(header->origin, "/:response");
	ActorFreeHeaderStruct(header);
	json_decref(responseJson);
	ActorFreeSplitMessage(znpSplitMessage);
	ActorSend(speakerActor, responseTopic, responseMessage, NULL, FALSE);
	free(responseMessage);
	free(responseTopic);
}

static void SpeakerActorCreate(char* guid, char* psw, char* host, WORD port)
{
	speakerActor = ActorCreate(guid, psw, host, port);
	//Register callback to handle request package
	if (speakerActor == NULL)
	{
		printf("Couldn't create actor\n");
		return;
	}
	ActorRegisterCallback(speakerActor, ":request/play", SpeakerActorOnPlayRequest, CALLBACK_RETAIN);
	ActorRegisterCallback(speakerActor, ":request/stop_playing", SpeakerActorOnStopRequest, CALLBACK_RETAIN);
}
/*
static void QrActorPublishQrContent(char* content)
{
	if (speakerActor == NULL) return;
	json_t* eventJson = json_object();
	json_t* paramsJson = json_object();
	json_t* contentJson = json_string(content);
	json_object_set(paramsJson, "content", contentJson);
	json_object_set(eventJson, "params", paramsJson);
	char* eventMessage = json_dumps(eventJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	char* topicName = ActorMakeTopicName(speakerActor->guid, "/:event/qr_update");
	ActorSend(speakerActor, topicName, eventMessage, NULL, FALSE);
	json_decref(contentJson);
	json_decref(paramsJson);
	json_decref(eventJson);
	free(topicName);
	free(eventMessage);
}

static void QrActorPublishInfo(char* info)
{
	if (speakerActor == NULL) return;
	json_t* eventJson = json_object();
	json_t* paramsJson = json_object();
	json_t* infoJson = json_string(info);
	json_object_set(paramsJson, "info", infoJson);
	json_object_set(eventJson, "params", paramsJson);
	char* eventMessage = json_dumps(eventJson, JSON_INDENT(4) | JSON_REAL_PRECISION(4));
	char* topicName = ActorMakeTopicName(speakerActor->guid, "/:event/info");
	ActorSend(speakerActor, topicName, eventMessage, NULL, FALSE);
	json_decref(infoJson);
	json_decref(paramsJson);
	json_decref(eventJson);
	free(topicName);
	free(eventMessage);
}
*/


void SpeakerActorStart(PACTOROPTION option)
{
	mosquitto_lib_init();
	SpeakerActorCreate(option->guid, option->psw, option->host, option->port);
	if (speakerActor == NULL)
	{
		mosquitto_lib_cleanup();
		return;
	}
	// set audio to 3.5mm
	system("amixer cset numit=3 1");
	while(1)
	{
		ActorProcessEvent(speakerActor);
		mosquitto_loop(speakerActor->client, 0, 1);
		usleep(10000);
	}
	mosquitto_disconnect(speakerActor->client);
	mosquitto_destroy(speakerActor->client);
	mosquitto_lib_cleanup();
}

int main(int argc, char* argv[])
{
	/* get option */
	int opt= 0;
	char *token = NULL;
	char *guid = NULL;
	char *host = NULL;
	WORD port = 0;
	printf("start speaker-service \n");
	// specific the expected option
	static struct option long_options[] = {
			{"id",      required_argument,  0, 'i' },
			{"token", 	required_argument,  0, 't' },
			{"host", 	required_argument,  0, 'H' },
			{"port", 	required_argument, 	0, 'p' },
			{"help", 	no_argument, 		0, 'h'	}
	};
	int long_index;
	/* Process option */
	while ((opt = getopt_long(argc, argv,":hi:t:H:p:",
			long_options, &long_index )) != -1) {
		switch (opt) {
		case 'h' :
			printf("using: speaker-service --<token> --<id> --<host> --port<>\n"
					"id: guid of the actor\n"
					"token: password of the actor\n"
					"host: mqtt server address, if omitted using local host\n"
					"port: mqtt port, if omitted using port 1883\n");
			return (EXIT_SUCCESS);
			break;
		case 'i':
			guid = StrDup(optarg);
			break;
		case 't':
			token = StrDup(optarg);
			break;
		case 'H':
			host = StrDup(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case ':':
			if (optopt == 'i')
			{
				printf("invalid option(s), using --help for help\n");
				return EXIT_FAILURE;
			}
			break;
		default:
			break;
		}
	}
	if (guid == NULL)
	{
		printf("invalid option(s), using --help for help\n");
		return EXIT_FAILURE;
	}
	ACTOROPTION ActorOpt;
	ActorOpt.guid = guid;
	ActorOpt.psw = token;
	ActorOpt.port = port;
	ActorOpt.host = host;
	SpeakerActorStart(&ActorOpt);
	return EXIT_SUCCESS;
}
