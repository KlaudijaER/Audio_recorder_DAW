#include <JuceHeader.h>
using namespace std;
using namespace juce;

class AudioRecorderComponent : public AudioAppComponent, //main audio recording component
    public Timer
{
public:
    AudioRecorderComponent()
        : thumbnailCache(5),
        thumbnail(2048, formatManager, thumbnailCache)
    {
        setSize(800, 400);

        formatManager.registerBasicFormats();// registers the formats

        addAndMakeVisible(recordButton);
        recordButton.setButtonText("Record");
        recordButton.onClick = [this] { startRecording(); };

        addAndMakeVisible(stopButton);
        stopButton.setButtonText("Stop");
        stopButton.onClick = [this] { stopRecording(); };
        stopButton.setEnabled(false);

        setAudioChannels(2, 2); //2input chanels and 2 output chanels as it is stereo not mono
        startTimer(40);//updates my user interface
    }

    ~AudioRecorderComponent() override//used in video, to override parents function to mine so it would work
    {
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override //shows that it is virtual function becuase of the override said in another video explainingit why it uses that word
    {
        this->sampleRate = sampleRate;
    }

    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override //it has like audio data from Juce itself and it stores the audio i make
    {
        if (isRecording)
        {
            const ScopedLock sl(writerLock);//it is used to lock a thread so i can write only one not multiple

            if (activeWriter.load() != nullptr)
            {
                activeWriter.load()->write(bufferToFill.buffer->getArrayOfReadPointers(),//writes audio buffer to file
                    bufferToFill.numSamples);

                thumbnail.addBlock(nextSampleNum, *bufferToFill.buffer,//adds audio to thumbnail for visualization
                    bufferToFill.startSample, bufferToFill.numSamples);
                nextSampleNum += bufferToFill.numSamples;
                playheadPosition = nextSampleNum / sampleRate;//calculates time
            }

            auto* channelData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample);// calculates audio lever for meter display at the exact moment
            float sum = 0.0f;

            for (int i = 0; i < bufferToFill.numSamples; ++i)
            {
                sum += abs(channelData[i]);
            }

            currentLevel = sum / bufferToFill.numSamples;
        }

        bufferToFill.clearActiveBufferRegion();//clears output buffer
    }

    void releaseResources() override//when aduio is stopped gets called to cleanuo resources
    {
    }

    void paint(Graphics& g) override // where i daw GUI and it is called outomatically from triggering repaint(), g is short for graphics
    {
        g.fillAll(Colours::darkgrey); //here is where i made the looks to see the recording or gui and the colors i ccan change and so on

        g.setColour(Colours::white);
        g.setFont(20.0f);
        g.drawText("Audio Recorder",
            getLocalBounds().removeFromTop(40),
            Justification::centred);

        auto waveformArea = getLocalBounds().reduced(20).withTrimmedTop(50).withTrimmedBottom(80);//waveform display
        g.setColour(Colours::black);
        g.fillRect(waveformArea);

        g.setColour(Colours::lightgreen); //drawing the waveform when i record, had few problems here asked ai for help
        if (thumbnail.getTotalLength() > 0.0 || isRecording)
        {
            double displayLength = thumbnail.getTotalLength();

            if (isRecording && nextSampleNum > 0)
            {
                displayLength = nextSampleNum / sampleRate;
            }

            if (displayLength > 0.0)
            {
                thumbnail.drawChannels(g, waveformArea, 0.0, displayLength, 1.0f);
            }
        }

        if (isRecording) //drawing a red indication line to know where i record just like in the other DAWs
        {
            float playheadX = waveformArea.getRight() - 2;  // Right edge

            g.setColour(Colours::red);
            g.drawLine(playheadX, waveformArea.getY(),
                playheadX, waveformArea.getBottom(),
                2.0f);
        }

        auto meterArea = getLocalBounds().removeFromBottom(60).reduced(20);//level meter of how loud is the sound
        g.setColour(Colours::white);
        g.drawText("Level:", meterArea.removeFromLeft(60), Justification::centredLeft);

        g.setColour(Colours::black);
        g.fillRect(meterArea);

        int barWidth = (int)(currentLevel * meterArea.getWidth() * 10.0f);
        barWidth = jmin(barWidth, meterArea.getWidth());

        auto levelBar = meterArea.withWidth(barWidth);
        g.setColour(Colours::lime);
        g.fillRect(levelBar);

        g.setColour(Colours::white);//i update the user about the status
        g.setFont(14.0f);
        String statusText = isRecording ? "RECORDING..." : "Ready";
        if (!isRecording && lastRecording.exists())
        {
            statusText = "Last recording: " + lastRecording.getFileName();
        }
        g.drawText(statusText, getLocalBounds().removeFromBottom(20), Justification::centred);
    }

    void resized() override //layout when i resize my window
    {
        auto area = getLocalBounds().removeFromTop(60).reduced(10);
        recordButton.setBounds(area.removeFromLeft(100));
        area.removeFromLeft(10);
        stopButton.setBounds(area.removeFromLeft(100));
    }

    void timerCallback() override// triggers repainting
    {
        repaint(); //repaints the GUI
    }

private:
    void startRecording() // recording itself and what is inside it
    {
        if (!isRecording)
        {
            auto parentDir = File::getSpecialLocation(File::userDocumentsDirectory);//puts the recording in the wanted folder
            auto recordingsFolder = parentDir.getChildFile("JUCE_records");
            recordingsFolder.createDirectory(); //ai helped me to get the folder working as i missed some edits and used wrong variable names

            lastRecording = recordingsFolder.getChildFile("Recording_" +
                Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".wav");// generating name for the recording day and time

            if (lastRecording.exists())
                lastRecording.deleteFile();

            unique_ptr<FileOutputStream> fileStream(lastRecording.createOutputStream());

            if (fileStream != nullptr)
            {
                WavAudioFormat wavFormat;//as i used wav files to save recordings and over all those files then here is the handling

                unique_ptr<AudioFormatWriter> writer;//wav writere with sterio input 16 bits and no metadata and quality parameter is 0 as it isnt needed for wav files
                writer.reset(wavFormat.createWriterFor(fileStream.get(),
                    sampleRate,
                    2,
                    16,
                    {},
                    0));

                if (writer != nullptr)
                {
                    fileStream.release();
                    thumbnail.reset(2, sampleRate);// resets thumbnail for new recording
                    nextSampleNum = 0;
                    playheadPosition = 0.0;

                    backgroundThread.startThread();//starts background thread to write into the file

                    threadedWriter.reset(new AudioFormatWriter::ThreadedWriter(writer.release(),// create threa writet to not block the audio thread
                        backgroundThread,
                        32768));

                    const ScopedLock sl(writerLock);
                    activeWriter = threadedWriter.get();//for thread saftey

                    isRecording = true;
                    DBG("Recording started!");//this is when i had problems about my code debug putput
                    recordButton.setEnabled(false);//disable record button
                    stopButton.setEnabled(true);//enable stop button
                }
            }
        }
    }

    void stopRecording() // here i stop recording
    {
        if (isRecording)
        {
            isRecording = false;
            DBG("Recording stopped!");

            {
                const ScopedLock sl(writerLock);
                activeWriter = nullptr;//signal to audi thread to stop writing
            }

            threadedWriter.reset();//close file and clear the buffer

            //when i added the waverform to show as i recorded this wasnt needed but im keeping it in as in future i might need to use it
            //this is help from ai as i had problems with saving or something, as i only heard like a second not a minute
            // Wait a moment for file to be fully written
            //Thread::sleep(100);

            //recordButton.setEnabled(true);
            //stopButton.setEnabled(false);

            //// Load the recording for display
            //if (lastRecording.exists())
            //{
            //    thumbnail.setSource(new FileInputSource(lastRecording));
            //    repaint(); // Force redraw
            //}
        }
    }

    //UI componets
    TextButton recordButton;
    TextButton stopButton;

    //for making the audio
    AudioFormatManager formatManager;//audio file format
    AudioThumbnailCache thumbnailCache;//caches thubnail data
    AudioThumbnail thumbnail;//waveform visualization

    TimeSliceThread backgroundThread{ "Audio Recorder Thread" };//background thread for file
    unique_ptr<AudioFormatWriter::ThreadedWriter> threadedWriter;//thread safe file writer

    File lastRecording;

    //thread saftey for writer to access 
    atomic<AudioFormatWriter::ThreadedWriter*> activeWriter{ nullptr };
    CriticalSection writerLock;

    //just state variables
    bool isRecording = false;
    double sampleRate = 44100.0;
    float currentLevel = 0.0f;
    int64_t nextSampleNum = 0; //counts how many audio samples there are up until moment
    double playheadPosition = 0.0; //where the red pointer line is put in seconds

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorderComponent)
};

//==============================================================================
class MainWindow : public DocumentWindow //main application window
{
public:
    MainWindow(String name)
        : DocumentWindow(name,
            Desktop::getInstance().getDefaultLookAndFeel()
            .findColour(ResizableWindow::backgroundColourId),
            DocumentWindow::allButtons)//standart buttons
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new AudioRecorderComponent(), true);//sets main component

#if JUCE_IOS || JUCE_ANDROID //in case of changing screans in this case phones then it would be a full screen
        setFullScreen(true);
#else
        setResizable(true, true);//at desktop i can resize the window
        centreWithSize(getWidth(), getHeight());
#endif

        setVisible(true);
    }

    void closeButtonPressed() override //close button or the window
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

//==============================================================================
class AudioRecorderApplication : public JUCEApplication //class for main application
{
public:
    AudioRecorderApplication() {}

    const String getApplicationName() override { return "Audio Recorder"; }
    const String getApplicationVersion() override { return "1.0.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const String& commandLine) override //stating the app
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override //shutting down the app
    {
        mainWindow = nullptr;//triggering to clear and delete window or just closing it
    }

    void systemRequestedQuit() override // to let the user quit the system to handle requests
    {
        quit();
    }

private:
    unique_ptr<MainWindow> mainWindow;
};

//==============================================================================
START_JUCE_APPLICATION(AudioRecorderApplication) // the start or entry to the program