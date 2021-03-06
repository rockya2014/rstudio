/*
 * SessionConsoleProcess.hpp
 *
 * Copyright (C) 2009-17 by RStudio, Inc.
 *
 * Unless you have received this program directly from RStudio pursuant
 * to the terms of a commercial license agreement with RStudio, then
 * this program is licensed to you under the terms of version 3 of the
 * GNU Affero General Public License. This program is distributed WITHOUT
 * ANY EXPRESS OR IMPLIED WARRANTY, INCLUDING THOSE OF NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Please refer to the
 * AGPL (http://www.gnu.org/licenses/agpl-3.0.txt) for more details.
 *
 */
#ifndef SESSION_CONSOLE_PROCESS_HPP
#define SESSION_CONSOLE_PROCESS_HPP

#include <session/SessionConsoleProcessInfo.hpp>

#include <deque>

#include <boost/regex.hpp>
#include <boost/signals.hpp>
#include <boost/circular_buffer.hpp>
#include <boost/enable_shared_from_this.hpp>

#include <core/system/Process.hpp>

#include <session/SessionConsoleProcessSocket.hpp>

namespace rstudio {
namespace core {
   class Error;
}
}

namespace rstudio {
namespace session {
namespace console_process {

const int kFlushSequence = -2; // see ShellInput.FLUSH_SEQUENCE
const int kIgnoreSequence = -1; // see ShellInput.IGNORE_SEQUENCE
const int kAutoFlushLength = 20;

class ConsoleProcess;
typedef boost::shared_ptr<ConsoleProcess> ConsoleProcessPtr;

class ConsoleProcess : boost::noncopyable,
                       public boost::enable_shared_from_this<ConsoleProcess>
{
private:
   // This constructor is only for resurrecting orphaned processes (i.e. for
   // suspend/resume scenarios)
   ConsoleProcess(boost::shared_ptr<ConsoleProcessInfo> procInfo);

   ConsoleProcess(
         const std::string& command,
         const core::system::ProcessOptions& options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo);
  
   ConsoleProcess(
         const std::string& program,
         const std::vector<std::string>& args,
         const core::system::ProcessOptions& options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo);
   
   void regexInit();
   void commonInit();

public:
   struct Input
   {
      explicit Input(const std::string& text, bool echoInput = true)
         : interrupt(false), text(text), echoInput(echoInput),
           sequence(kIgnoreSequence)
      {
      }

      explicit Input(int sequence, const std::string& text, bool echoInput = true)
         : interrupt(false), text(text), echoInput(echoInput), sequence(sequence)
      {
      }

      Input() : interrupt(false), echoInput(false), sequence(kIgnoreSequence) {}

      bool empty() { return !interrupt && text.empty(); }

      bool interrupt ;
      std::string text;
      bool echoInput;

      // used to reassemble out-of-order input messages
      int sequence;
   };

public:
   // creating console processes with a command string is not supported on
   // Win32 because in order to implement the InteractionPossible/Always
   // modes we use the consoleio.exe proxy, which can only be invoked from
   // the runProgram codepath
   static ConsoleProcessPtr create(
         const std::string& command,
         core::system::ProcessOptions options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo);

   static ConsoleProcessPtr create(
         const std::string& program,
         const std::vector<std::string>& args,
         core::system::ProcessOptions options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo);

   static ConsoleProcessPtr createTerminalProcess(
         core::system::ProcessOptions options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo,
         bool enableWebsockets);

   static ConsoleProcessPtr createTerminalProcess(
         core::system::ProcessOptions options,
         boost::shared_ptr<ConsoleProcessInfo> procInfo);

   static ConsoleProcessPtr createTerminalProcess(
         ConsoleProcessPtr proc);

   // Configure ProcessOptions for a terminal and return it. Also sets
   // the output param pSelectedShellType to indicate which shell type
   // was actually configured (e.g. what did 'default' get mapped to?).
   static core::system::ProcessOptions createTerminalProcOptions(
         TerminalShell::TerminalShellType desiredShellType,
         int cols, int rows,
         int termSequence,
         core::FilePath workingDir,
         TerminalShell::TerminalShellType *pSelectedShellType);

   virtual ~ConsoleProcess() {}

   // set a custom prompt handler -- return true to indicate the prompt
   // was handled and false to let it pass. return empty input to
   // indicate the user cancelled out of the prompt (in this case the
   // process will be terminated)
   void setPromptHandler(
         const boost::function<bool(const std::string&, Input*)>& onPrompt);

   boost::signal<void(int)>& onExit() { return onExit_; }

   std::string handle() const { return procInfo_->getHandle(); }
   InteractionMode interactionMode() const { return procInfo_->getInteractionMode(); }

   core::Error start();
   void enqueInput(const Input& input);
   Input dequeInput();
   void enquePrompt(const std::string& prompt);
   void interrupt();
   void interruptChild();
   void resize(int cols, int rows);
   void onSuspend();
   bool isStarted() const { return started_; }
   void setCaption(std::string& caption) { procInfo_->setCaption(caption); }
   std::string getCaption() const { return procInfo_->getCaption(); }
   void setTitle(std::string& title) { procInfo_->setTitle(title); }
   std::string getTitle() const { return procInfo_->getTitle(); }
   void deleteLogFile(bool lastLineOnly = false) const;
   void setNotBusy() { procInfo_->setHasChildProcs(false); }
   bool getIsBusy() const { return procInfo_->getHasChildProcs(); }
   bool getAllowRestart() const { return procInfo_->getAllowRestart(); }
   std::string getChannelMode() const;
   int getTerminalSequence() const { return procInfo_->getTerminalSequence(); }
   int getBufferLineCount() const { return procInfo_->getBufferLineCount(); }
   int getCols() const { return procInfo_->getCols(); }
   int getRows() const { return procInfo_->getRows(); }
   PidType getPid() const { return pid_; }
   bool getAltBufferActive() const { return procInfo_->getAltBufferActive(); }
   core::FilePath getCwd() const { return procInfo_->getCwd(); }
   bool getWasRestarted() const { return procInfo_->getRestarted(); }

   std::string getShellName() const;
   TerminalShell::TerminalShellType getShellType() const {
      return procInfo_->getShellType();
   }

   // Used to downgrade to RPC mode after failed attempt to connect websocket
   void setRpcMode();

   // Get the given (0-based) chunk of the saved buffer; if more is available
   // after the requested chunk, *pMoreAvailable will be set to true
   std::string getSavedBufferChunk(int chunk, bool* pMoreAvailable) const;

   // Get the full terminal buffer
   std::string getBuffer() const;

   void setShowOnOutput(bool showOnOutput) const { procInfo_->setShowOnOutput(showOnOutput); }

   core::json::Object toJson() const;
   static ConsoleProcessPtr fromJson( core::json::Object& obj);

   void onReceivedInput(const std::string& input);

private:
   core::system::ProcessCallbacks createProcessCallbacks();
   bool onContinue(core::system::ProcessOperations& ops);
   void onStdout(core::system::ProcessOperations& ops,
                 const std::string& output);
   void onExit(int exitCode);
   void onHasSubprocs(bool hasSubProcs);
   void reportCwd(const core::FilePath& cwd);
   void processQueuedInput(core::system::ProcessOperations& ops);

   std::string bufferedOutput() const;
   void enqueOutputEvent(const std::string& output);
   void enquePromptEvent(const std::string& prompt);
   void handleConsolePrompt(core::system::ProcessOperations& ops,
                            const std::string& prompt);
   void maybeConsolePrompt(core::system::ProcessOperations& ops,
                           const std::string& output);

   ConsoleProcessSocketConnectionCallbacks createConsoleProcessSocketConnectionCallbacks();
   void onConnectionOpened();
   void onConnectionClosed();

private:
   // Command and options that will be used when start() is called
   std::string command_;
   std::string program_;
   std::vector<std::string> args_;
   core::system::ProcessOptions options_;
   boost::shared_ptr<ConsoleProcessInfo> procInfo_;

   // Whether the process should be stopped
   bool interrupt_;

   // Whether to send pty interrupt
   bool interruptChild_;
   
   // Whether the tty should be notified of a resize
   int newCols_; // -1 = no change
   int newRows_; // -1 = no change

   // Last known PID of associated process
   PidType pid_;

   // Has client been notified of state of childProcs_ at least once?
   bool childProcsSent_;

   // Pending input (writes or ptyInterrupts)
   std::deque<Input> inputQueue_;
   int lastInputSequence_;

   boost::function<bool(const std::string&, Input*)> onPrompt_;
   boost::signal<void(int)> onExit_;

   // regex for prompt detection
   boost::regex controlCharsPattern_;
   boost::regex promptPattern_;

   // is the underlying process started?
   bool started_;

   // cached pointer to process options, for use in websocket thread callbacks
   boost::weak_ptr<core::system::ProcessOperations> pOps_;
   boost::mutex mutex_;
};

core::json::Array processesAsJson();
core::Error initialize();

} // namespace console_process
} // namespace session
} // namespace rstudio

#endif // SESSION_CONSOLE_PROCESS_HPP
