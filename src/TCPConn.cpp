#include <stdexcept>
#include <strings.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include "TCPConn.h"
#include "strfuncts.h"
#include <crypto++/secblock.h>
#include <crypto++/osrng.h>
#include <crypto++/filters.h>
#include <crypto++/rijndael.h>
#include <crypto++/gcm.h>
#include <crypto++/aes.h>
// Voltz include 
#include <random>


using namespace CryptoPP;

// Common defines for this TCPConn
const unsigned int iv_size = AES::BLOCKSIZE;
const unsigned int key_size = AES::DEFAULT_KEYLENGTH;
const unsigned int auth_size = 16;

/**********************************************************************************************
 * TCPConn (constructor) - creates the connector and initializes - creates the command strings
 *                         to wrap around network commands
 *
 *    Params: key - reference to the pre-loaded AES key
 *            verbosity - stdout verbosity - 3 = max
 *
 **********************************************************************************************/

TCPConn::TCPConn(LogMgr &server_log, CryptoPP::SecByteBlock &key, unsigned int verbosity):
                                    _data_ready(false),
                                    _aes_key(key),
                                    _verbosity(verbosity),
                                    _server_log(server_log)
{
   // prep some tools to search for command sequences in data
   uint8_t slash = (uint8_t) '/';
   c_rep.push_back((uint8_t) '<');
   c_rep.push_back((uint8_t) 'R');
   c_rep.push_back((uint8_t) 'E');
   c_rep.push_back((uint8_t) 'P');
   c_rep.push_back((uint8_t) '>');

   c_endrep = c_rep;
   c_endrep.insert(c_endrep.begin()+1, 1, slash);

   c_ack.push_back((uint8_t) '<');
   c_ack.push_back((uint8_t) 'A');
   c_ack.push_back((uint8_t) 'C');
   c_ack.push_back((uint8_t) 'K');
   c_ack.push_back((uint8_t) '>');

   c_auth.push_back((uint8_t) '<');
   c_auth.push_back((uint8_t) 'A');
   c_auth.push_back((uint8_t) 'U');
   c_auth.push_back((uint8_t) 'T');
   c_auth.push_back((uint8_t) '>');

   c_endauth = c_auth;
   c_endauth.insert(c_endauth.begin()+1, 1, slash);

   c_sid.push_back((uint8_t) '<');
   c_sid.push_back((uint8_t) 'S');
   c_sid.push_back((uint8_t) 'I');
   c_sid.push_back((uint8_t) 'D');
   c_sid.push_back((uint8_t) '>');

   c_endsid = c_sid;
   c_endsid.insert(c_endsid.begin()+1, 1, slash);

}


TCPConn::~TCPConn() {

}

/**********************************************************************************************
 * accept - simply calls the acceptFD FileDesc method to accept a connection on a server socket.
 *
 *    Params: server - an open/bound server file descriptor with an available connection
 *
 *    Throws: socket_error for recoverable errors, runtime_error for unrecoverable types
 **********************************************************************************************/

bool TCPConn::accept(SocketFD &server) {
   // Accept the connection
   bool results = _connfd.acceptFD(server);


   // Set the state as waiting for the authorization packet
   // Voltz change 
   //_status = s_connected;
   _status = s_schallenge;
   _connected = true;
   return results;
}

/**********************************************************************************************
 * sendData - sends the data in the parameter to the socket
 *
 *    Params:  msg - the string to be sent
 *             size - if we know how much data we should expect to send, this should be populated
 *
 *    Throws: runtime_error for unrecoverable errors
 **********************************************************************************************/

bool TCPConn::sendData(std::vector<uint8_t> &buf) {
   
   _connfd.writeBytes<uint8_t>(buf);
   
   return true;
}

/**********************************************************************************************
 * sendEncryptedData - sends the data in the parameter to the socket after block encrypting it
 *
 *    Params:  msg - the string to be sent
 *             size - if we know how much data we should expect to send, this should be populated
 *
 *    Throws: runtime_error for unrecoverable errors
 **********************************************************************************************/

bool TCPConn::sendEncryptedData(std::vector<uint8_t> &buf) {

   // Encrypt
   encryptData(buf);

   // And send!
   return sendData(buf);
}

/**********************************************************************************************
 * encryptData - block encrypts data and places the results in the buffer in <ID><Data> format
 *
 *    Params:  buf - where to place the <IV><Data> stream
 *
 *    Throws: runtime_error for unrecoverable errors
 **********************************************************************************************/

void TCPConn::encryptData(std::vector<uint8_t> &buf) {
   // For the initialization vector
   SecByteBlock init_vector(iv_size);
   AutoSeededRandomPool rnd;

   // Generate our random init vector
   rnd.GenerateBlock(init_vector, init_vector.size());

   // Encrypt the data
   CFB_Mode<AES>::Encryption encryptor;
   encryptor.SetKeyWithIV(_aes_key, _aes_key.size(), init_vector);

   std::string cipher;
   ArraySource as(buf.data(), buf.size(), true,
            new StreamTransformationFilter(encryptor, new StringSink(cipher)));

   // Now add the IV to the stream we will be sending out
   std::vector<uint8_t> enc_data(init_vector.begin(), init_vector.end());
   enc_data.insert(enc_data.end(), cipher.begin(), cipher.end());
   buf = enc_data;
}

/**********************************************************************************************
 * handleConnection - performs a check of the connection, looking for data on the socket and
 *                    handling it based on the _status, or stage, of the connection
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::handleConnection() {

   try {
      switch (_status) {

         // Client: Just connected, send our SID
         case s_connecting:
            //std::cout << "Client SENDING SID" << std::endl;
            sendSID();
            break;

	 // Server: Send first challenge Voltz
	 case s_schallenge:
            //std::cout << "Server Sending Challenge" << std::endl;
            sAuthChallenge(_auth_challenge_s);
            break;

         // Client: Encrypt first challenge and send it back Voltz
         case s_cproof:
            //std::cout << "Client Proving itself" << std::endl;
            cAuthResponse();
            break;
            
	 // Server: Verifies the encrypted response from client Voltz
	 case s_scheck:
            //std::cout << "Server Verifying Client" << std::endl;
            sAuthCheck();
            break;
	    
         // Client: Sends challenge to server Voltz
         case s_cchallenge:
            //std::cout << "Client sending Challenge" << std::endl;
            cAuthChallenge(_auth_challenge_c);
            break;

         // Server: respond with encrypted challenge and send it back Voltz
         case s_sproof:
            //std::cout << "Server Proving itself" << std::endl;
            sAuthResponse();
            break;

         // Client: Verify the encrypted response from server Voltz
         case s_ccheck:
            //std::cout << "Client Verifying Server" << std::endl;
            cAuthCheck();
	    break;

         // Server: Wait for the SID from a newly-connected client, then send our SID
         case s_connected:
            //std::cout << "Server Recieve SID" << std::endl;
            waitForSID();
            break;
   
         // Client: connecting user - replicate data
         case s_datatx:
            transmitData();
            break;

         // Server: Receive data from the client
         case s_datarx:
            waitForData();
            break;
   
         // Client: Wait for acknowledgement that data sent was received before disconnecting
         case s_waitack:
            awaitAck();
            break;
         
         // Server: Data received and conn disconnected, but waiting for the data to be retrieved
         case s_hasdata:
            break;

         default:
            std::cout << _status << std::endl;
            throw std::runtime_error("Invalid connection status in connection!");
            break;
      }
   } catch (socket_error &e) {
      std::cout << "Socket error, disconnecting.\n";
      disconnect();
      return;
   }

}

/**********************************************************************************************
 * sendSID()  - Client: after a connection, client sends its Server ID to the server
 *
 *    Throws: socket_error for network issues, runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::sendSID() {
   std::vector<uint8_t> buf(_svr_id.begin(), _svr_id.end());
   wrapCmd(buf, c_sid, c_endsid);
   sendData(buf);

   _status = s_datatx; 
}

/**********************************************************************************************
 * getRandBits  - Creates random bits useful for sending challenges. Voltz
 * 
 *		This method is based on generating bits from a discussion with LT Josh Larson
 *              and a reference from cplusplus.com/reference/random/
 *              and a reference from en.cppreference.com/w/cpp/numeric/random
 *    
 **********************************************************************************************/

void TCPConn::getRandBits(std::vector<uint8_t> &dest) {
   int size = 16;
   // initialize c++ way of creating random numbers 
   std::default_random_engine byteGen{std::random_device{}()};
   std::uniform_int_distribution<uint8_t> dist(0,255);

   // ensure the vector is the right size 
   dest.reserve(size);
   while(dest.size() < size)
   {
      dest.emplace_back(0);
   }   

   // assign the random characters 
   for(auto & r : dest){
	r = dist(byteGen);
   }

}

/**********************************************************************************************
 * sAuthChallenge  - create a challenge and send it to other side of connection Voltz
 * 
 *             dest: the vector that the challenge will be stored in (server challenge)
 *    
 **********************************************************************************************/

void TCPConn::sAuthChallenge(std::vector<uint8_t> &dest) {
   // clear buffer if anything is there
   if (_connfd.hasData()) {
   std::vector<uint8_t> holder;   
   getData(holder);
   }
   //  put random bits into challenge 
   getRandBits(dest);
   std::vector<uint8_t> buf(dest.begin(), dest.end()); 
   
   // send the info to the attempted connection, not encrypted 
   wrapCmd(buf, c_auth, c_endauth);
   sendData(buf);

/*
         std::cout << "_auth_challenge_s IS: " << std::endl;
         for(auto i : _auth_challenge_s)
         {
            std::cout << i;// << std::endl;
         }
         std::cout << std::endl;
*/

   // set the status to the next step
   _status = s_scheck;

}

/**********************************************************************************************
 * cAuthChallenge  - create a challenge and send it to the server Voltz
 * 
 *             dest: the vector that the challenge will be stored in (client challenge)
 *    
 **********************************************************************************************/

void TCPConn::cAuthChallenge(std::vector<uint8_t> &dest) {
   // wait for information
   if (_connfd.hasData()) {
      std::vector<uint8_t> holder;
      getData(holder);
   
      //  put random bits into challenge 
      getRandBits(dest);
      std::vector<uint8_t> buf(dest.begin(), dest.end()); 
   
      // send the info to the attempted connection, not encrypted 
      wrapCmd(buf, c_auth, c_endauth);
      sendData(buf);

      // set the status to the next step
      _status = s_ccheck;
   }

}

/**********************************************************************************************
 * sAuthResponse  - Gets the Client challenge from the socket and responds to challenge 
 * 
 *    
 **********************************************************************************************/

void TCPConn::sAuthResponse() {
   // If data on the socket, should be challenge from server
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      // quit if getting the data fails
      if (!getData(buf)){
         std::cout << "FAILED TO GET DATA" << std::endl;
         return;   
      }                           

      // if the command is in an unexpected format 
      if (!getCmdData(buf, c_auth, c_endauth)) {
         std::stringstream msg;
         msg << "Challenge String from server invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      // send encrypted data
      wrapCmd(buf, c_auth, c_endauth);
      sendEncryptedData(buf);
      // set status to next step
      _status = s_connected;
   }
}

/**********************************************************************************************
 * cAuthResponse  - Gets the Server challenge from the socket and responds to challenge 
 * 
 *    
 **********************************************************************************************/

void TCPConn::cAuthResponse() {
   // If data on the socket, should be challenge from server
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      // quit if getting the data fails
      if (!getData(buf)){
         std::cout << "FAILED TO GET DATA" << std::endl;
         return;   
      }                           

      // if the command is in an unexpected format 
      if (!getCmdData(buf, c_auth, c_endauth)) {
         std::stringstream msg;
         msg << "Challenge String from server invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         std::cout << msg.str() << std::endl; //Voltz
         disconnect();
         return;
      }

      // send encrypted data
      wrapCmd(buf, c_auth, c_endauth);
      sendEncryptedData(buf);
      // set status to next step
      _status = s_cchallenge;
   }
}

/**********************************************************************************************
 * sAuthCheck  - Check response to challenge 
 * 
 *    
 **********************************************************************************************/

void TCPConn::sAuthCheck() {
   // If data on the socket, should be response from client
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;
      // quit if getting the data fails
      if (!getEncryptedData(buf))
         return;
      // if the command is in an unexpected format 
      if (!getCmdData(buf, c_auth, c_endauth)) {
         std::stringstream msg;
         msg << "Challenge String in invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      // Check against saved variable and move on or disconnect 
      if(buf == _auth_challenge_s){ // case that the client response to challenge is valid
         // set status to allow client to send challenge
         _status = s_sproof;
         // send something across so client knows to continue
         std::vector <uint8_t> buff;
         buff.emplace_back(1);
         wrapCmd(buff, c_auth, c_endauth);
         sendData(buff);
      }
      else{
        std::cout << "Failed the server authorization check. Exiting" << std::endl;
	disconnect();
        return;
      }

   }
}

/**********************************************************************************************
 * cAuthCheck  - Check response to challenge 
 * 
 *    
 **********************************************************************************************/

void TCPConn::cAuthCheck() {
   // If data on the socket, should be challenge from server
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;
      // quit if getting the data fails
      if (!getEncryptedData(buf))
         return;
      // if the command is in an unexpected format 
      if (!getCmdData(buf, c_auth, c_endauth)) {
         std::stringstream msg;
         msg << "Challenge String in invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      // Check against saved variable and move on or disconnect 
      if(buf == _auth_challenge_c){ // case that server response to challenge is valid
	 // switch status to carry on since both sides have validated 
         _status = s_connecting;
      }
      else{
        std::cout << "Failed a authorization check. Exiting" << std::endl;
	disconnect();
        return;
      }

   }
}

/**********************************************************************************************
 * waitForSID()  - receives the SID and sends our SID
 *
 *    Throws: socket_error for network issues, runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::waitForSID() {

   // If data on the socket, should be our Auth string from our host server
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      if (!getData(buf))
         return;

      if (!getCmdData(buf, c_sid, c_endsid)) {
         std::stringstream msg;
         msg << "SID string from connecting client invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      std::string node(buf.begin(), buf.end());
      setNodeID(node.c_str());


      // Send our Node ID
      buf.assign(_svr_id.begin(), _svr_id.end());
      wrapCmd(buf, c_sid, c_endsid);
      sendData(buf);

      _status = s_datarx;

   }
}


/**********************************************************************************************
 * transmitData()  - receives the SID from the server and transmits data
 *
 *    Throws: socket_error for network issues, runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::transmitData() {

   // If data on the socket, should be our Auth string from our host server
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      if (!getData(buf))
         return;

      if (!getCmdData(buf, c_sid, c_endsid)) {
         std::stringstream msg;
         msg << "SID string from connected server invalid format. Cannot authenticate.";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      std::string node(buf.begin(), buf.end());
      setNodeID(node.c_str());

      // Send the replication data
      sendData(_outputbuf);

      if (_verbosity >= 3)
         std::cout << "Successfully authenticated connection with " << getNodeID() <<
                      " and sending replication data.\n";

      // Wait for their response
      _status = s_waitack;
   }
}


/**********************************************************************************************
 * waitForData - receiving server, authentication complete, wait for replication datai
               Also sends a plaintext random auth string of our own
 *
 *    Throws: socket_error for network issues, runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::waitForData() {

   // If data on the socket, should be replication data
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      if (!getData(buf))
         return;

      if (!getCmdData(buf, c_rep, c_endrep)) {
         std::stringstream msg;
         msg << "Replication data possibly corrupted from" << getNodeID() << "\n";
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return;
      }

      // Got the data, save it
      _inputbuf = buf;
      _data_ready = true;

      // Send the acknowledgement and disconnect
      sendData(c_ack);

      if (_verbosity >= 2)
         std::cout << "Successfully received replication data from " << getNodeID() << "\n";


      disconnect();
      _status = s_hasdata;
   }
}


/**********************************************************************************************
 * awaitAwk - waits for the awk that data was received and disconnects
 *
 *    Throws: socket_error for network issues, runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::awaitAck() {

   // Should have the awk message
   if (_connfd.hasData()) {
      std::vector<uint8_t> buf;

      if (!getData(buf))
         return;

      if (findCmd(buf, c_ack) == buf.end())
      {
         std::stringstream msg;
         msg << "Awk expected from data send, received something else. Node:" << getNodeID() << "\n";
         _server_log.writeLog(msg.str().c_str());
      }
  
      if (_verbosity >= 3)
         std::cout << "Data ack received from " << getNodeID() << ". Disconnecting.\n";

 
      disconnect();
   }
}

/**********************************************************************************************
 * getData - Reads in data from the socket and checks to see if there's an end command to the
 *           message to confirm we got it all
 *
 *    Params: None - data is stored in _inputbuf for retrieval with GetInputData
 *
 *    Returns: true if the data is ready to be read, false if they lost connection
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/

bool TCPConn::getData(std::vector<uint8_t> &buf) {

   std::vector<uint8_t> readbuf;
   size_t count = 0;

   buf.clear();

   while (_connfd.hasData()) {
      // read the data on the socket up to 1024
      count += _connfd.readBytes<uint8_t>(readbuf, 1024);

      // check if we lost connection
      if (readbuf.size() == 0) {
         std::stringstream msg;
         std::string ip_addr;
         msg << "Connection from server " << _node_id << " lost (IP: " << 
                                                         getIPAddrStr(ip_addr) << ")"; 
         _server_log.writeLog(msg.str().c_str());
         disconnect();
         return false;
      }

      buf.insert(buf.end(), readbuf.begin(), readbuf.end());

      // concat the data onto anything we've read before
//      _inputbuf.insert(_inputbuf.end(), readbuf.begin(), readbuf.end());
   }
   return true;
}

/**********************************************************************************************
 * decryptData - Takes in an encrypted buffer in the form IV/Data and decrypts it, replacing
 *               buf with the decrypted info (destroys IV string>
 *
 *    Params: buf - the encrypted string and holds the decrypted data (minus IV)
 *
 **********************************************************************************************/
void TCPConn::decryptData(std::vector<uint8_t> &buf) {
   // For the initialization vector
   SecByteBlock init_vector(iv_size);

   // Copy the IV from the incoming stream of data
   init_vector.Assign(buf.data(), iv_size);
   buf.erase(buf.begin(), buf.begin() + iv_size);

   // Decrypt the data
   CFB_Mode<AES>::Decryption decryptor;
   decryptor.SetKeyWithIV(_aes_key, _aes_key.size(), init_vector);

   std::string recovered;
   ArraySource as(buf.data(), buf.size(), true,
            new StreamTransformationFilter(decryptor, new StringSink(recovered)));

   buf.assign(recovered.begin(), recovered.end());

}


/**********************************************************************************************
 * getEncryptedData - Reads in data from the socket and decrypts it, passing the decrypted
 *                    data back in buf
 *
 *    Params: None - data is stored in _inputbuf for retrieval with GetInputData
 *
 *    Returns: true if the data is ready to be read, false otherwise
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/

bool TCPConn::getEncryptedData(std::vector<uint8_t> &buf) {
   // Get the data from the socket
   if (!getData(buf))
      return false;

   decryptData(buf);

   return true; 
}

/**********************************************************************************************
 * findCmd - returns an iterator to the location of a string where a command starts
 * hasCmd - returns true if command was found, false otherwise
 *
 *    Params: buf = the data buffer to look for the command within
 *            cmd - the command string to search for in the data
 *
 *    Returns: iterator - points to cmd position if found, end() if not found
 *
 **********************************************************************************************/

std::vector<uint8_t>::iterator TCPConn::findCmd(std::vector<uint8_t> &buf, std::vector<uint8_t> &cmd) {
   return std::search(buf.begin(), buf.end(), cmd.begin(), cmd.end());
}

bool TCPConn::hasCmd(std::vector<uint8_t> &buf, std::vector<uint8_t> &cmd) {
   return !(findCmd(buf, cmd) == buf.end());
}

/**********************************************************************************************
 * getCmdData - looks for a startcmd and endcmd and returns the data between the two 
 *
 *    Params: buf = the string to search for the tags
 *            startcmd - the command at the beginning of the data sought
 *            endcmd - the command at the end of the data sought
 *
 *    Returns: true if both start and end commands were found, false otherwisei
 *
 **********************************************************************************************/

bool TCPConn::getCmdData(std::vector<uint8_t> &buf, std::vector<uint8_t> &startcmd, 
                                                    std::vector<uint8_t> &endcmd) {
   std::vector<uint8_t> temp = buf;
   auto start = findCmd(temp, startcmd);
   auto end = findCmd(temp, endcmd);

   if ((start == temp.end()) || (end == temp.end()) || (start == end))
      return false;

   buf.assign(start + startcmd.size(), end);
   return true;
}

/**********************************************************************************************
 * wrapCmd - wraps the command brackets around the passed-in data
 *
 *    Params: buf = the string to wrap around
 *            startcmd - the command at the beginning of the data
 *            endcmd - the command at the end of the data
 *
 **********************************************************************************************/

void TCPConn::wrapCmd(std::vector<uint8_t> &buf, std::vector<uint8_t> &startcmd,
                                                    std::vector<uint8_t> &endcmd) {
   std::vector<uint8_t> temp = startcmd;
   temp.insert(temp.end(), buf.begin(), buf.end());
   temp.insert(temp.end(), endcmd.begin(), endcmd.end());

   buf = temp;
}


/**********************************************************************************************
 * getReplData - Returns the data received on the socket and marks the socket as done
 *
 *    Params: buf = the data received
 *
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/

void TCPConn::getInputData(std::vector<uint8_t> &buf) {

   // Returns the replication data off this connection, then prepares it to be removed
   buf = _inputbuf;

   _data_ready = false;
   _status = s_none;
}

/**********************************************************************************************
 * connect - Opens the socket FD, attempting to connect to the remote server
 *
 *    Params:  ip_addr - ip address string to connect to
 *             port - port in host format to connect to
 *
 *    Throws: socket_error exception if failed. socket_error is a child class of runtime_error
 **********************************************************************************************/

void TCPConn::connect(const char *ip_addr, unsigned short port) {

   // Set the status to initial client proof Voltz
   _status = s_cproof;

   // Try to connect
   if (!_connfd.connectTo(ip_addr, port))
      throw socket_error("TCP Connection failed!");

   _connected = true;
}

// Same as above, but ip_addr and port are in network (big endian) format
void TCPConn::connect(unsigned long ip_addr, unsigned short port) {
   // Set the status to initial client proof Voltz
   _status = s_cproof;

   if (!_connfd.connectTo(ip_addr, port))
      throw socket_error("TCP Connection failed!");

   _connected = true;
}

/**********************************************************************************************
 * assignOutgoingData - sets up the connection so that, at the next handleConnection, the data
 *                      is sent to the target server
 *
 *    Params:  data - the data stream to send to the server
 *
 **********************************************************************************************/

void TCPConn::assignOutgoingData(std::vector<uint8_t> &data) {

   _outputbuf.clear();
   _outputbuf = c_rep;
   _outputbuf.insert(_outputbuf.end(), data.begin(), data.end());
   _outputbuf.insert(_outputbuf.end(), c_endrep.begin(), c_endrep.end());
}
 

/**********************************************************************************************
 * disconnect - cleans up the socket as required and closes the FD
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/
void TCPConn::disconnect() {
   _connfd.closeFD();
   _connected = false;
}


/**********************************************************************************************
 * isConnected - performs a simple check on the socket to see if it is still open 
 *
 *    Throws: runtime_error for unrecoverable issues
 **********************************************************************************************/
bool TCPConn::isConnected() {
   return _connected;
   // return _connfd.isOpen(); // This does not work very well
}

/**********************************************************************************************
 * getIPAddrStr - gets a string format of the IP address and loads it in buf
 *
 **********************************************************************************************/
const char *TCPConn::getIPAddrStr(std::string &buf) {
   _connfd.getIPAddrStr(buf);
   return buf.c_str();
}

