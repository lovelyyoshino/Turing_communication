#include <iostream>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>   
#include<jsoncpp/json/json.h>
#include<curl/curl.h>
#include<exception>

#include "robot_voice/qtts.h"
#include "robot_voice/msp_cmn.h"
#include "robot_voice/msp_errors.h"

#include "ros/ros.h"
#include "std_msgs/String.h"

#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>


bool start=0;
std::string result="未知";
/* wav音频头部格式 */
typedef struct _wave_pcm_hdr
{
	char            riff[4];                // = "RIFF"
	int		size_8;                 // = FileSize - 8
	char            wave[4];                // = "WAVE"
	char            fmt[4];                 // = "fmt "
	int		fmt_size;		// = 下一个结构体的大小 : 16

	short int       format_tag;             // = PCM : 1
	short int       channels;               // = 通道数 : 1
	int		samples_per_sec;        // = 采样率 : 8000 | 6000 | 11025 | 16000
	int		avg_bytes_per_sec;      // = 每秒字节数 : samples_per_sec * bits_per_sample / 8
	short int       block_align;            // = 每采样点字节数 : wBitsPerSample / 8
	short int       bits_per_sample;        // = 量化比特数: 8 | 16

	char            data[4];                // = "data";
	int		data_size;              // = 纯数据长度 : FileSize - 44 
} wave_pcm_hdr;

/* 默认wav音频头部数据 */
wave_pcm_hdr default_wav_hdr = 
{
	{ 'R', 'I', 'F', 'F' },
	0,
	{'W', 'A', 'V', 'E'},
	{'f', 'm', 't', ' '},
	16,
	1,
	1,
	16000,
	32000,
	2,
	16,
	{'d', 'a', 't', 'a'},
	0  
};
/* 文本合成 */
int text_to_speech(const char* src_text, const char* des_path, const char* params)
{
	int          ret          = -1;
	FILE*        fp           = NULL;
	const char*  sessionID    = NULL;
	unsigned int audio_len    = 0;
	wave_pcm_hdr wav_hdr      = default_wav_hdr;
	int          synth_status = MSP_TTS_FLAG_STILL_HAVE_DATA;

	if (NULL == src_text || NULL == des_path)
	{
		printf("params is error!\n");
		return ret;
	}
	fp = fopen(des_path, "wb");
	if (NULL == fp)
	{
		printf("open %s error.\n", des_path);
		return ret;
	}
	/* 开始合成 */
	sessionID = QTTSSessionBegin(params, &ret);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionBegin failed, error code: %d.\n", ret);
		fclose(fp);
		return ret;
	}
	ret = QTTSTextPut(sessionID, src_text, (unsigned int)strlen(src_text), NULL);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSTextPut failed, error code: %d.\n",ret);
		QTTSSessionEnd(sessionID, "TextPutError");
		fclose(fp);
		return ret;
	}
	//else
	//{
	//	printf("QTTSTextPut success");
	//}
	
	//printf("正在合成 ...\n");
	fwrite(&wav_hdr, sizeof(wav_hdr) ,1, fp); //添加wav音频头，使用采样率为16000
	while (1) 
	{
		/* 获取合成音频 */
		const void* data = QTTSAudioGet(sessionID, &audio_len, &synth_status, &ret);
		if (MSP_SUCCESS != ret)
			break;
		if (NULL != data)
		{
			fwrite(data, audio_len, 1, fp);
		    wav_hdr.data_size += audio_len; //计算data_size大小
		}
		if (MSP_TTS_FLAG_DATA_END == synth_status)
			break;
		printf(">");
		usleep(150*1000); //防止频繁占用CPU
	}//合成状态synth_status取值请参阅《讯飞语音云API文档》
	printf("\n");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSAudioGet failed, error code: %d.\n",ret);
		QTTSSessionEnd(sessionID, "AudioGetError");
		fclose(fp);
		return ret;
	}
	/* 修正wav文件头数据的大小 */
	wav_hdr.size_8 += wav_hdr.data_size + (sizeof(wav_hdr) - 8);
	
	/* 将修正过的数据写回文件头部,音频文件为wav格式 */
	fseek(fp, 4, 0);
	fwrite(&wav_hdr.size_8,sizeof(wav_hdr.size_8), 1, fp); //写入size_8的值
	fseek(fp, 40, 0); //将文件指针偏移到存储data_size值的位置
	fwrite(&wav_hdr.data_size,sizeof(wav_hdr.data_size), 1, fp); //写入data_size的值
	fclose(fp);
	fp = NULL;
	/* 合成完毕 */
	ret = QTTSSessionEnd(sessionID, "Normal");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionEnd failed, error code: %d.\n",ret);
	}

	return ret;
}

std::string to_string(int val) 
{
    char buf[20];
    sprintf(buf, "%d", val);
    return std::string(buf);
}
 
int writer(char *data, size_t size, size_t nmemb, std::string *writerData)
{
    
    if (writerData == NULL)
        return -1;
    unsigned long sizes = size * nmemb;
    writerData->append(data, sizes);
 
    return sizes;
}
 
int parseJsonResonse(std::string input)
{
   Json::Value root;
   Json::Reader reader;
   bool parsingSuccessful = reader.parse(input, root);
   if(!parsingSuccessful)
   {
       std::cout<<"!!! Failed to parse the response data"<< std::endl;
        return -1;
   }
   const Json::Value code = root["intent"]["code"];
   const Json::Value text = root["results"][0]["values"]["text"];
   result = text.asString();
 
   std::cout<< "response code:" << code << std::endl;
   std::cout<< "response text:" << result << std::endl;
   return 0;
}
 
int HttpPostRequest(std::string input)
{
    std::string buffer;

    std::string strJson = "{";
    strJson += "\"perception\" : ";
    strJson += "{";
    strJson += "\"inputText\" : ";
    strJson += "{";
    strJson += "\"text\" : ";
    strJson += "\"";
    strJson += input;
    strJson += "\"";
    strJson += "}";
    strJson += "},";
    strJson += "\"userInfo\" : ";
    strJson += "{";
    strJson += "\"apiKey\" : \"5ea0f11bxxxxx9c52a47849387484\",";  //这个是你创建一个机器人之后自动生成的，将其复制过来就OK了
    strJson += "\"userId\" : \"xxxxx\""; //这是是你注册的时候自动生成的，同样是复制过来就OK了
    strJson += "}";
    strJson += "}";
 
    std::cout<<"post json string: " << strJson << std::endl;
 
     try
    {
        CURL *pCurl = NULL;
        CURLcode res;
        // In windows, this will init the winsock stuff
        curl_global_init(CURL_GLOBAL_ALL);
 
        // get a curl handle
        pCurl = curl_easy_init();
        if (NULL != pCurl)
        {
            // 设置超时时间为8秒
            curl_easy_setopt(pCurl, CURLOPT_TIMEOUT, 8);
 
            // First set the URL that is about to receive our POST.
            curl_easy_setopt(pCurl, CURLOPT_URL, "http://openapi.tuling123.com/openapi/api/v2");
            //curl_easy_setopt(pCurl, CURLOPT_URL, "http://192.168.0.2/posttest.cgi");
 
            // 设置http发送的内容类型为JSON
            curl_slist *plist = curl_slist_append(NULL,"Content-Type:application/json;charset=UTF-8");
            curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, plist);
 
            // 设置要POST的JSON数据
            curl_easy_setopt(pCurl, CURLOPT_POSTFIELDS, strJson.c_str());
 
            curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, writer);
 
            curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &buffer);
 
            // Perform the request, res will get the return code
            res = curl_easy_perform(pCurl);
            // Check for errors
            if (res != CURLE_OK)
            {
                printf("curl_easy_perform() failed:%s\n", curl_easy_strerror(res));
            }
            // always cleanup
            curl_easy_cleanup(pCurl);
        }
        curl_global_cleanup();
    }
    catch (std::exception &ex)
    {
        printf("curl exception %s.\n", ex.what());
    }
    if(buffer.empty())
    {
      std::cout<< "!!! ERROR The Tuling sever response NULL" << std::endl;
      return 0;
    }
    else
    {
        parseJsonResonse(buffer);
        return 1;
    }
 
    
 
}


void voiceWordsCallback(const std_msgs::String::ConstPtr& msg)
{
    char cmd[2000];
    const char* text;
    int         ret                  = MSP_SUCCESS;
    const char* session_begin_params = "voice_name = xiaoyan, text_encoding = utf8, sample_rate = 16000, speed = 50, volume = 50, pitch = 50, rdn = 2";
    const char* filename             = "tts_sample.wav"; //合成的语音文件名称

    std::string dataString = msg->data;

    if (strstr(dataString.c_str(),(char*)"小了小了") != NULL)
        {
            dataString="小乐小乐。";
        }

    std::cout<<"I heard :"<<dataString<<std::endl;

    if(dataString.compare("你可以做什么？") == 0 && start==1)
    {
        char helpString[40] = "我可以寻迹";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("小乐小乐。") == 0 && start==1)
    {
        char helpString[40] = "我在";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("前进。") == 0 && start==1)
    {
        char helpString[40] = "前进中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("后退。") == 0 && start==1)
    {
        char helpString[40] = "后退中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("左转。") == 0 && start==1)
    {
        char helpString[40] = "左转中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("右转。") == 0 && start==1)
    {
        char helpString[40] = "右转中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("左平移。") == 0 && start==1)
    {
        char helpString[40] = "左平移中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("右平移。") == 0 && start==1)
    {
        char helpString[40] = "右平移中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(strstr(dataString.c_str(),(char*)"导航去") != NULL && start==1)
    {
        char helpString[40] = "导航中";
        text = helpString;
        std::cout<<text<<std::endl;
    }
    else if(dataString.compare("现在时间。") == 0 && start==1)
    {
        //获取当前时间
        struct tm *ptm; 
        long ts; 

        ts = time(NULL); 
        ptm = localtime(&ts); 
        std::string string = "现在时间" + to_string(ptm-> tm_hour) + "点" + to_string(ptm-> tm_min) + "分";

        char timeString[40];
        string.copy(timeString, sizeof(string), 0);
        text = timeString;
        std::cout<<text<<std::endl;
    }
	else if(dataString.compare("结束。") == 0 && start==1)
    {
        char closeString[40] = "关闭语音";
        text = closeString;
        std::cout<<text<<std::endl;
		start=0;
    }
    else if (start==1)
    {
        if(HttpPostRequest(dataString)==1)
        {
        	//char otherString[200]=result;
        	text = (char*)result.data();
        	std::cout<<text<<std::endl;

        }
    }

    if(dataString.compare("开启。") == 0 && start==0)
    {
        char nameString[40] = "开启语音";
        text = nameString;
        std::cout<<text<<std::endl;
		start=1;
    }

	if (text!="" && start==1)
	{
		/* 文本合成 */
		printf("开始合成 ...\n");
    	ret = text_to_speech(text, filename, session_begin_params);
	
		if (MSP_SUCCESS != ret)
		{
			printf("text_to_speech failed, error code: %d.\n", ret);
		}
		printf("合成完毕\n");
		printf("\r\n");
		printf("\r\n");
		printf("\r\n");


		unlink("/tmp/cmd");  
		mkfifo("/tmp/cmd", 0777);  
		popen("mplayer -quiet -slave -input file=/tmp/cmd 'tts_sample.wav'","r");

		//sleep(3);
	}
}

void toExit()
{
    printf("按任意键退出 ...\n");
    getchar();
    MSPLogout(); //退出登录
}

int main(int argc, char* argv[])
{
	int         ret                  = MSP_SUCCESS;
	const char* login_params         = "appid = 56ee43d0, work_dir = .";//登录参数,appid与msc库绑定,请勿随意改动
	/*
	* rdn:           合成音频数字发音方式
	* volume:        合成音频的音量
	* pitch:         合成音频的音调
	* speed:         合成音频对应的语速
	* voice_name:    合成发音人
	* sample_rate:   合成音频采样率
	* text_encoding: 合成文本编码格式
	*
	* 详细参数说明请参阅《讯飞语音云MSC--API文档》
	*/

	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, login_params);//第一个参数是用户名，第二个参数是密码，第三个参数是登录参数，用户名和密码可在http://open.voicecloud.cn注册获取
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed, error code: %d.\n", ret);
		/*goto exit ;*///登录失败，退出登录
        toExit();
	}
	printf("\n###########################################################################\n");
	printf("## 开启语音合成##\n");
	printf("###########################################################################\n\n");


    ros::init(argc,argv,"TextToSpeech");
    ros::NodeHandle n;
    ros::Subscriber sub =n.subscribe("voiceWords", 1000,voiceWordsCallback);
    ros::spin();

exit:
	printf("按任意键退出 ...\n");
	getchar();
	MSPLogout(); //退出登录

	return 0;
}

