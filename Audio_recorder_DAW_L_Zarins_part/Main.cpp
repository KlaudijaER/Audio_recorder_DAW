#include <JuceHeader.h>
using namespace std;
using namespace juce;

// Forward declaration of main component
class AudioRecorderComponent;

//==============================================================================
// Panel Components - UI building blocks
//==============================================================================

// Top Menu Bar - the applications header
class MenuBar : public Component
{
public:
    MenuBar()
    {
        setSize(1200, 50); // Fixed size: 1200 pixels wide, 50 pixels tall
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xFF4A4A4A)); // Dark grey background color
    }
};

// Editing Tools Bar with Record/Stop buttons and Level Meter
class EditingToolsPanel : public Component
{
public:
    EditingToolsPanel(AudioRecorderComponent& owner); // Constructor takes reference to parent

    void paint(Graphics& g) override; // Draws the level meter
    void resized() override; // Positions the buttons
    void updateRecordingState(bool isRecording); // Enables/disables buttons based on state

private:
    AudioRecorderComponent& parentComponent; // Reference to main component to call its methods
    TextButton recordButton; // Red "Record" button
    TextButton stopButton; // Dark red "Stop" button
};

// Left Side Track Controls - creation for each recording track
class TrackControlsPanel : public Component
{
public:
    TrackControlsPanel()
    {
        setSize(100, 120);

        // Add a mute button and make it visible
        addAndMakeVisible(muteButton);
        muteButton.setButtonText("mute");

        // Add a solo button and make it visible
        addAndMakeVisible(soloButton);
        soloButton.setButtonText("solo");
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xFF6B6B6B)); // Medium grey background
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(5); // Get bounds with 5px padding
        muteButton.setBounds(area.removeFromTop(30)); // Top 30 pixels for mute button
        area.removeFromTop(5); // 5 pixel spacing
        soloButton.setBounds(area.removeFromTop(30)); // Next 30 pixels for solo button
    }

private:
    TextButton muteButton; // Mute button (not functional)
    TextButton soloButton; // Solo button (not functional)
};

// Recording Display Area - shows waveform during and after recording
class RecordingDisplayPanel : public Component
{
public:
    RecordingDisplayPanel(AudioRecorderComponent& owner, int index); // Takes parent and track index

    void paint(Graphics& g) override; // Draws waveform and delete button
    void mouseDown(const MouseEvent& event) override; // Handles clicking the X button
    void setRecordingIndex(int newIndex) { recordingIndex = newIndex; } // Updates which recording this displays
    int getRecordingIndex() const { return recordingIndex; } // Returns current recording index

private:
    AudioRecorderComponent& parentComponent; // Reference to main component to access recordings
    int recordingIndex; // Which recording in the array this panel displays
};

// Bottom Controls Panel - the applications footer
class BottomControlsPanel : public Component
{
public:
    BottomControlsPanel()
    {
        setSize(1200, 80);
    }

    void paint(Graphics& g) override
    {
        g.fillAll(Colour(0xFF6B6B6B)); // Medium grey background
    }
};

//==============================================================================
// Single Recording Track, combining controls and display into one unit
// Each recording is created like - controls on left, waveform on right
//==============================================================================
class RecordingTrack : public Component
{
public:
    RecordingTrack(AudioRecorderComponent& owner, int index)
        : parentComponent(owner),
        trackIndex(index),
        controls(new TrackControlsPanel()), // Create new control panel
        display(new RecordingDisplayPanel(owner, index)) // Create new display panel
    {
        // Make both sub-components visible
        addAndMakeVisible(controls.get());
        addAndMakeVisible(display.get());
        setSize(1100, 120);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        controls->setBounds(area.removeFromLeft(100)); // Left 100 pixels for controls
        display->setBounds(area); // Remaining space for waveform display
    }

    // Getters to access sub-components
    TrackControlsPanel* getControls() { return controls.get(); }
    RecordingDisplayPanel* getDisplay() { return display.get(); }
    int getTrackIndex() const { return trackIndex; }

    // Update track index so that recordings are deleted to fix remaining indexes
    void setTrackIndex(int newIndex)
    {
        trackIndex = newIndex; // Update this track's index
        display->setRecordingIndex(newIndex); // Update display's index too
    }

private:
    AudioRecorderComponent& parentComponent; // Reference to main component
    int trackIndex; // The recording number
    unique_ptr<TrackControlsPanel> controls; // Smart pointer that owns the controls panel
    unique_ptr<RecordingDisplayPanel> display; // Smart pointer that owns the display panel
};

//==============================================================================
// Scrollable container for recordings
// Holds all RecordingTrack components and manages their layout
//==============================================================================
class RecordingsContainer : public Component
{
public:
    RecordingsContainer()
    {
        setSize(1100, 600);
    }

    void paint(Graphics& g) override
    {
        // No background needed
    }

    void addRecordingTrack(RecordingTrack* track)
    {
        addAndMakeVisible(track); // Add track to this container
        tracks.push_back(track); // Add to our vector for tracking
        updateLayout(); // Recalculate positions
    }

    void removeRecordingTrack(RecordingTrack* track)
    {
        // Loop through vector
        for (int i = 0; i < tracks.size(); i++)
        {
            if (tracks[i] == track) // Track to remove
            {
                tracks.erase(tracks.begin() + i); // Remove from vector
                break;
            }
        }
        updateLayout(); // Recalculate positions for remaining tracks
    }

    void updateLayout()
    {
        int yPos = 10; // Start 10 pixels from top
        // Positions each track vertically
        for (int i = 0; i < tracks.size(); i++)
        {
            tracks[i]->setBounds(0, yPos, 1100, 120); // Set position and size
            yPos += 130; // Move down for next track (120 height + 10 spacing)
        }

        // If there are many tracks, container grows to fit them all
        int totalHeight = tracks.size() * 130 + 10;
        setSize(1100, jmax(600, totalHeight)); // At least 600px
    }

    vector<RecordingTrack*>& getTracks() { return tracks; } // Returns reference to tracks vector

private:
    vector<RecordingTrack*> tracks; // Stores pointers to all recording tracks
};

//==============================================================================
// Mostly Klaudijas part - Audio Recorder Component
// Brain of the application that handles all audio recording logic
//==============================================================================
class AudioRecorderComponent : public AudioAppComponent, //main audio recording component
    public Timer // Inherits timer functionality for regular updates
{
public:
    AudioRecorderComponent()
        : editingTools(*this), // Initialize editing tools panel with reference to this
        recordingsContainer(new RecordingsContainer()) // Create new recordings container
    {
        setSize(1200, 800);

        formatManager.registerBasicFormats(); // registers the formats

        // Add all main panels to the window
        addAndMakeVisible(menuBar);
        addAndMakeVisible(editingTools);
        addAndMakeVisible(bottomControls);

        // Add viewport with recordings container to make it scrollable
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(recordingsContainer.get(), false); // Set container as scrollable content
        viewport.setScrollBarsShown(true, false); // Show vertical scrollbar, hide horizontal

        setAudioChannels(2, 2); //2input chanels and 2 output chanels as it is stereo not mono
        startTimer(40); //updates my user interface
    }

    ~AudioRecorderComponent() override //used in video, to override parents function to mine so it would work
    {
        shutdownAudio();
    }

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override //shows that it is virtual function because of the override said in another video explainingit why it uses that word
    {
        this->sampleRate = sampleRate;
    }

    void getNextAudioBlock(const AudioSourceChannelInfo& bufferToFill) override //it has like audio data from Juce itself and it stores the audio i make
    {
        if (isRecording)
        {
            const ScopedLock sl(writerLock); //it is used to lock a thread so i can write only one not multiple

            if (activeWriter.load() != nullptr)
            {
                activeWriter.load()->write(bufferToFill.buffer->getArrayOfReadPointers(), //writes audio buffer to file
                    bufferToFill.numSamples);

                // Add audio data to the thumbnail for waveform visualization
                if (currentRecordingIndex >= 0 && currentRecordingIndex < recordingThumbnails.size())
                {
                    recordingThumbnails[currentRecordingIndex]->addBlock(nextSampleNum, *bufferToFill.buffer,
                        bufferToFill.startSample, bufferToFill.numSamples);
                }
                nextSampleNum += bufferToFill.numSamples;
                playheadPosition = nextSampleNum / sampleRate; //calculates time
            }

            auto* channelData = bufferToFill.buffer->getReadPointer(0, bufferToFill.startSample); // calculates audio lever for meter display at the exact moment
            float sum = 0.0f;

            for (int i = 0; i < bufferToFill.numSamples; ++i)
            {
                sum += abs(channelData[i]);
            }

            currentLevel = sum / bufferToFill.numSamples;
        }

        bufferToFill.clearActiveBufferRegion(); //clears output buffer
    }
    //=================================================================================
    // Klaudijas part - END 
    //=================================================================================
    void releaseResources() override {} // Called when audio stops - nothing to cleanup

    void paint(Graphics& g) override
    {
        g.fillAll(Colours::darkgrey); // Background color for main window
    }

    void resized() override
    {
        // Called when window is resized - recalculates all component positions
        auto area = getLocalBounds(); // Get total window area
        menuBar.setBounds(area.removeFromTop(50)); // Top menu bar (50px height)
        editingTools.setBounds(area.removeFromTop(40)); // Editing tools bar with Record/Stop/Level (40px height) 
        area.removeFromTop(10); // Small white space (10px)
        bottomControls.setBounds(area.removeFromBottom(80)); // Bottom controls (footer - 80px height)
        area.removeFromBottom(10); // Small white space before footer (10px)
        viewport.setBounds(area); // Viewport area (remaining space - full width)
    }

    void timerCallback() override
    {
        editingTools.updateRecordingState(isRecording); // Called every 40ms by the timer - used for updating UI

        // Repaint all recording displays
        auto& tracks = recordingsContainer->getTracks();
        for (int i = 0; i < tracks.size(); i++)
        {
            tracks[i]->getDisplay()->repaint(); // Force redraw of each waveform
        }
        repaint(); // Repaint main window
    }

    //=================================================================================
    // Mostly Klaudijas part - Start recording 
    //=================================================================================
    void startRecording() // recording itself and what is inside it
    {
        // Sloppy way to check max recordings limit
        if (recordingThumbnails.size() >= 6) // Check if already at maximum
        {
            // Show error popup
            AlertWindow::showAsync(
                MessageBoxOptions()
                .withTitle("Error")
                .withMessage("Maximum 6 recordings allowed! Please delete some recordings first.")
                .withButton("OK"),
                nullptr
            );
            return; // Exit function - don't start recording
        }

        if (!isRecording)
        {
            // Create filename with timestamp
            auto parentDir = File::getSpecialLocation(File::userDocumentsDirectory); //puts the recording in the wanted folder
            File newRecording = parentDir.getChildFile("Recording_" +
                Time::getCurrentTime().formatted("%Y%m%d_%H%M%S") + ".wav"); // generating name for the recording day and time

            if (newRecording.exists())
                newRecording.deleteFile();

            unique_ptr<FileOutputStream> fileStream(newRecording.createOutputStream());

            if (fileStream != nullptr)
            {
                WavAudioFormat wavFormat; //as i used wav files to save recordings and over all those files then here is the handling

                unique_ptr<AudioFormatWriter> writer; //wav writere with sterio input 16 bits and no metadata and quality parameter is 0 as it isnt needed for wav files
                writer.reset(wavFormat.createWriterFor(fileStream.get(),
                    sampleRate,
                    2,
                    16,
                    {},
                    0));

                if (writer != nullptr)
                {
                    fileStream.release();

                    // Create new thumbnail for this recording
                    AudioThumbnailCache* newCache = new AudioThumbnailCache(5);
                    AudioThumbnail* newThumbnail = new AudioThumbnail(2048, formatManager, *newCache);
                    newThumbnail->reset(2, sampleRate); // resets thumbnail for new recording

                    // Add to separate vectors
                    recordingCaches.push_back(newCache);
                    recordingThumbnails.push_back(newThumbnail);
                    recordingFiles.push_back(newRecording);

                    currentRecordingIndex = recordingThumbnails.size() - 1; // Index of new recording

                    // Create new track with controls and display
                    RecordingTrack* newTrack = new RecordingTrack(*this, currentRecordingIndex);
                    recordingsContainer->addRecordingTrack(newTrack); // Add to scrollable container

                    // Reset counters for new recording
                    nextSampleNum = 0;
                    playheadPosition = 0.0;

                    backgroundThread.startThread(); // Start background thread for file writing

                    threadedWriter.reset(new AudioFormatWriter::ThreadedWriter(writer.release(), // create threa writet to not block the audio thread
                        backgroundThread,
                        32768)); // Buffer size

                    const ScopedLock sl(writerLock);
                    activeWriter = threadedWriter.get(); //for thread saftey

                    isRecording = true;
                    DBG("Recording started!"); //this is when i had problems about my code debug putput
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
                activeWriter = nullptr; //signal to audi thread to stop writing
            }

            threadedWriter.reset(); //close file and clear the buffer

            // This is help from AI as I had problems with saving or something
            // Wait a moment for file to be fully written
            Thread::sleep(100); // Sleep 100ms to ensure file is complete

            // Load the recording for display
            if (currentRecordingIndex >= 0 && currentRecordingIndex < recordingFiles.size())
            {
                File lastFile = recordingFiles[currentRecordingIndex];
                if (lastFile.exists()) // Check if file was created successfully
                {
                    // Load complete file into thumbnail for full waveform display
                    recordingThumbnails[currentRecordingIndex]->setSource(new FileInputSource(lastFile));
                    DBG("Recording saved: " + lastFile.getFullPathName());
                }
            }

            // Show save dialog
            showSaveDialog();
        }
    }
    //=================================================================================
    // Klaudijas part - END
    //=================================================================================

    void showSaveDialog()
    {
        String fileName = "unknown";
        if (currentRecordingIndex >= 0 && currentRecordingIndex < recordingFiles.size())
        {
            fileName = recordingFiles[currentRecordingIndex].getFileName();
        }

        // Show popup with filename
        AlertWindow::showAsync(
            MessageBoxOptions()
            .withTitle("Save Recording")
            .withMessage("Recording saved as:\n" + fileName)
            .withButton("OK"),
            nullptr
        );
    }

    void deleteRecording(int index)
    {
        // Check if valid index
        if (index < 0) return;
        auto& tracks = recordingsContainer->getTracks();
        if (index >= tracks.size()) return;

        // Show confirmation dialog
        AlertWindow::showAsync(
            MessageBoxOptions()
            .withTitle("Delete Recording")
            .withMessage("Are you sure you want to delete this recording?")
            .withButton("Yes")
            .withButton("No"),
            [this, index](int result) // Lambda function called when user clicks button
            {
                if (result == 1) // Yes button = 1
                {
                    auto& tracks = recordingsContainer->getTracks();

                    // Manually go through arrays
                    // Delete the visual track component
                    if (index < tracks.size())
                    {
                        RecordingTrack* trackToDelete = tracks[index];
                        recordingsContainer->removeRecordingTrack(trackToDelete); // Remove from container
                        delete trackToDelete; // Delete the object
                    }

                    // Delete the thumbnail
                    if (index < recordingThumbnails.size())
                    {
                        delete recordingThumbnails[index];
                        recordingThumbnails.erase(recordingThumbnails.begin() + index);
                    }

                    // Delete the cache
                    if (index < recordingCaches.size())
                    {
                        delete recordingCaches[index];
                        recordingCaches.erase(recordingCaches.begin() + index);
                    }

                    // Delete the actual file from documents folder
                    if (index < recordingFiles.size())
                    {
                        File fileToDelete = recordingFiles[index]; // Get the file reference
                        if (fileToDelete.exists()) // Check if file exists on disk
                        {
                            fileToDelete.deleteFile(); // Delete the physical file
                            DBG("File deleted: " + fileToDelete.getFullPathName());
                        }
                        recordingFiles.erase(recordingFiles.begin() + index); // Remove from vector
                    }

                    // Go through each remaining track and fix their index
                    auto& remainingTracks = recordingsContainer->getTracks();
                    for (int i = 0; i < remainingTracks.size(); i++)
                    {
                        remainingTracks[i]->setTrackIndex(i); // Update to new index
                    }

                    repaint(); // Redraw everything
                }
            }
        );
    }

    // Getter methods - allow other components to access private data
    bool getIsRecording() const { return isRecording; }
    float getCurrentLevel() const { return currentLevel; }
    AudioThumbnail* getThumbnail(int index)
    {
        // Bounds checking
        if (index < 0) return nullptr;
        if (index >= recordingThumbnails.size()) return nullptr;
        return recordingThumbnails[index];
    }

    int64_t getNextSampleNum() const { return nextSampleNum; }
    double getPlayheadPosition() const { return playheadPosition; }
    double getSampleRate() const { return sampleRate; }

    int getCurrentRecordingIndex() const { return currentRecordingIndex; }

private:
    // UI Components
    MenuBar menuBar;
    EditingToolsPanel editingTools;
    BottomControlsPanel bottomControls;
    Viewport viewport;
    unique_ptr<RecordingsContainer> recordingsContainer;

    // Separate vectors instead of a proper struct
    vector<AudioThumbnail*> recordingThumbnails; // Waveform data for each recording
    vector<AudioThumbnailCache*> recordingCaches; // Cache for thumbnail generation
    vector<File> recordingFiles; // File paths for each recording

    // ==== Klaudijas part - START ====
    // Audio components
    AudioFormatManager formatManager; //audio file format

    TimeSliceThread backgroundThread{ "Audio Recorder Thread" }; //background thread for file
    unique_ptr<AudioFormatWriter::ThreadedWriter> threadedWriter; //thread safe file writer

    //thread saftey for writer to access 
    atomic<AudioFormatWriter::ThreadedWriter*> activeWriter{ nullptr };
    CriticalSection writerLock;

    //just state variables
    bool isRecording = false;
    double sampleRate = 44100.0;
    float currentLevel = 0.0f;
    int64_t nextSampleNum = 0;
    double playheadPosition = 0.0;
    // ==== Klaudijas part - END ====

    int currentRecordingIndex = -1; // Index of currently recording track (-1 = not recording)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioRecorderComponent)
};

//==============================================================================
// EditingToolsPanel implementation
//==============================================================================
EditingToolsPanel::EditingToolsPanel(AudioRecorderComponent& owner)
    : parentComponent(owner) // Store reference to parent
{
    // Setup Record button
    addAndMakeVisible(recordButton);
    recordButton.setButtonText("Record");
    recordButton.setColour(TextButton::buttonColourId, Colours::red); // Red background
    recordButton.onClick = [this] { parentComponent.startRecording(); }; // Lambda - calls startRecording when clicked

    // Setup Stop button
    addAndMakeVisible(stopButton);
    stopButton.setButtonText("Stop");
    stopButton.setColour(TextButton::buttonColourId, Colours::darkred); // Dark red background
    stopButton.onClick = [this] { parentComponent.stopRecording(); }; // Lambda - calls stopRecording when clicked
    stopButton.setEnabled(false); // Start disabled (can't stop if not recording)
}

void EditingToolsPanel::paint(Graphics& g)
{
    g.fillAll(Colours::white); // White background

    // Draw level meter on the right side
    auto meterArea = getLocalBounds().removeFromRight(400).reduced(5);

    g.setColour(Colours::black);
    g.setFont(12.0f);
    g.drawText("Level:", meterArea.removeFromLeft(50), Justification::centredLeft); // "Level:" label

    // Level meter background (black rectangle)
    g.setColour(Colours::black);
    g.fillRect(meterArea);

    // Level meter bar (green, shows current input level)
    if (parentComponent.getIsRecording()) // Only show when recording
    {
        float level = parentComponent.getCurrentLevel(); // Get current audio level
        int barWidth = (int)(level * meterArea.getWidth() * 10.0f); // Scale level to pixels (10x multiplier for visibility)
        barWidth = jmin(barWidth, meterArea.getWidth()); // Clamp to max width

        g.setColour(Colours::lime); // Bright green
        g.fillRect(meterArea.withWidth(barWidth)); // Draw bar from left
    }
}

void EditingToolsPanel::resized()
{
    auto area = getLocalBounds().reduced(5);

    // Position Record and Stop buttons on the left
    recordButton.setBounds(area.removeFromLeft(100));
    area.removeFromLeft(10);
    stopButton.setBounds(area.removeFromLeft(100));
}

void EditingToolsPanel::updateRecordingState(bool isRecording)
{
    recordButton.setEnabled(!isRecording); // Enable Record button only when NOT recording
    stopButton.setEnabled(isRecording); // Enable Stop button only when recording
    repaint(); // Redraw to update level meter
}

//==============================================================================
// RecordingDisplayPanel implementation
// Displays waveform, playhead, and delete button for one recording
//==============================================================================
RecordingDisplayPanel::RecordingDisplayPanel(AudioRecorderComponent& owner, int index)
    : parentComponent(owner), recordingIndex(index) // Store parent reference and index
{
}

void RecordingDisplayPanel::paint(Graphics& g)
{
    g.fillAll(Colour(0xFF3A3A3A)); // Dark grey

    g.setColour(Colours::black);
    g.drawRect(getLocalBounds(), 2); // 2 pixel thick black border

    // Draw waveform if available
    AudioThumbnail* thumbnail = parentComponent.getThumbnail(recordingIndex); // Get thumbnail for this recording
    if (thumbnail != nullptr) // Check if thumbnail exists
    {
        auto waveformArea = getLocalBounds().reduced(4); // Area inside border

        // Check if we should draw waveform
        if (thumbnail->getTotalLength() > 0.0 || // Has recorded data
            (parentComponent.getIsRecording() && parentComponent.getCurrentRecordingIndex() == recordingIndex)) // OR currently recording this track
        {
            double displayLength = thumbnail->getTotalLength(); // Get length in seconds

            // If currently recording THIS track, use live length
            if (parentComponent.getIsRecording() &&
                parentComponent.getCurrentRecordingIndex() == recordingIndex &&
                parentComponent.getNextSampleNum() > 0)
            {
                displayLength = parentComponent.getNextSampleNum() / parentComponent.getSampleRate(); // Calculate seconds from samples
            }

            if (displayLength > 0.0) // Only draw if theres something
            {
                g.setColour(Colours::lightgreen); // Light green waveform
                thumbnail->drawChannels(g, waveformArea, 0.0, displayLength, 1.0f); // Draw waveform
            }
        }

        if (parentComponent.getIsRecording() && parentComponent.getCurrentRecordingIndex() == recordingIndex)
        {
            float playheadX = waveformArea.getRight() - 2; // Right edge (minus 2px for visibility)

            g.setColour(Colours::red); // Red playhead line
            g.drawLine(playheadX, waveformArea.getY(),
                playheadX, waveformArea.getBottom(),
                2.0f); // Vertical line, 2 pixels thick
        }
    }

    // Draw red X button in lower left 
    Rectangle<int> xButton(5, getHeight() - 25, 20, 20); // Position and size
    g.setColour(Colours::red); // Red circle
    g.fillEllipse(xButton.toFloat()); // Draw filled circle

    g.setColour(Colours::white); // White text
    g.setFont(14.0f);
    g.drawText("X", xButton, Justification::centred); // Draw "X" centered
}

void RecordingDisplayPanel::mouseDown(const MouseEvent& event)
{
    // Check if clicked on X button
    Rectangle<int> xButton(5, getHeight() - 25, 20, 20); // Same position as in paint()

    if (xButton.contains(event.getPosition())) // Check if click was inside X button
    {
        parentComponent.deleteRecording(recordingIndex); // Call delete with this recording's index
    }
}

//==============================================================================
// MainWindow - The actual window that appears on screen
//==============================================================================
class MainWindow : public DocumentWindow
{
public:
    MainWindow(String name)
        : DocumentWindow(name,
            Desktop::getInstance().getDefaultLookAndFeel()
            .findColour(ResizableWindow::backgroundColourId), // Default background color
            DocumentWindow::allButtons) // Show minimize, maximize, close buttons
    {
        setUsingNativeTitleBar(true); // Use OS native title bar (Windows/Mac style)
        setContentOwned(new AudioRecorderComponent(), true); // Create and set main component

        setResizable(true, true); // Window can be resized
        centreWithSize(1200, 800); // Window screen size

        setVisible(true); // Show window
    }

    void closeButtonPressed() override
    {
        // Called when user clicks X to close window
        JUCEApplication::getInstance()->systemRequestedQuit(); // Tell application to quit
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

//==============================================================================
// AudioRecorderApplication - The application itself
//==============================================================================
class AudioRecorderApplication : public JUCEApplication
{
public:
    AudioRecorderApplication() {} // Empty constructor

    const String getApplicationName() override { return "Audio Recorder"; }
    const String getApplicationVersion() override { return "1.0.0"; } // Version number
    bool moreThanOneInstanceAllowed() override { return true; } // Allow multiple instances

    void initialise(const String& commandLine) override
    {
        // Called when application starts
        mainWindow.reset(new MainWindow(getApplicationName())); // Create main window
    }

    void shutdown() override
    {
        // Called when application closes
        mainWindow = nullptr; // Delete main window
    }

    void systemRequestedQuit() override
    {
        // Called when OS wants to quit (user clicked X, pressed Alt+F4, etc.)
        quit(); // Actually quit the application
    }

private:
    unique_ptr<MainWindow> mainWindow; // Smart pointer owns the window
};

//==============================================================================
// Application entry point - starts the application
START_JUCE_APPLICATION(AudioRecorderApplication)