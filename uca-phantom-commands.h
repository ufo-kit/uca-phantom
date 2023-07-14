#ifndef UCA_PHANTOM_COMMANDS_H
#define UCA_PHANTOM_COMMANDS_H

#include <gio/gio.h>
#include <gmodule.h>
#include <glib-object.h>

typedef struct _PhantomCommand PhantomCommand;
struct _PhantomCommand {
    const gchar *name;
    guint argc;
    gint property_id;
    // TODO: argument type ?
};

PhantomCommand Commands[] = {
    /**
     * @brief Set Variable or Structure (set)
     * 
     * @paragraph Sets a variable or structure value. When a structure is set, only the values of it’s members that are explicitly named in the tagged list are updated. Structure sets are atomic with respect to other protocol commands and the machine process. In addition, it is significantly more efficient to set a structure in a single command than to use separate commands for each member. Some sets have the side effect of triggering the machine process to update the changed values in the hardware. Since it is particularly important to make sure the values in a structure are consistent at all times, it is better to use aggregate sets.
     * 
     * @overload get <variable_name> 
     * @overload get <structure_name> 
     * @overload get *
     * 
     * @param variable_name string name of variable or structure. 
     * @param structure_name string name of structure
     * @param "*" The optional "*" argument signifies that all structures are returned.
     * 
     * @warning The total length of the command (from the command name to the last newline) must not exceed the protocol buffer size of 64k
     * 
     * @return The response is an ASCII string terminated with an unescaped CRLF. In most cases it will fit on a single line, and contains either a single value or a tagged list. If a structure is requested, the response is broke down into lines, one for each value, each line except the last being terminated with \CRLF. The last line is terminated with an unescaped CRLF.
     * 
     * @throws
     *  - ERR: expecting varname 
     *  - ERR: expecting . 
     *  - ERR: path too deep 
     *  - ERR: name <varname> is unknown 
     *  - ERR: Bad pathlen
     * 
    */
    {"get", 1, CMD_GET},
    /**
     * @brief Set Variable or Structure (set)
     * 
     * @paragraph Sets a variable or structure value. When a structure is set, only the values of it’s members that are explicitly named in the tagged list are updated. Structure sets are atomic with respect to other protocol commands and the machine process. In addition, it is significantly more efficient to set a structure in a single command than to use separate commands for each member. Some sets have the side effect of triggering the machine process to update the changed values in the hardware. Since it is particularly important to make sure the values in a structure are consistent at all times, it is better to use aggregate sets.
     * 
     * @overload set <variable_name> value
     * @overload set <structure_name> <tagged_list>
     * @overload set * <tagged_list>
     * 
     * @param variable_name string name of variable or structure. 
     * @param value string value to set variable or structure to.
     * @param structure_name string name of structure.
     * @param tagged_list string tagged list of structure members to set.
     * @param "*" signifies that the whole unit structure is set. A long tagged list can be split across multiple lines with \CR, \LF or \CRLF.
     * 
     * @warning The total length of the command (from the command name to the last newline) must not exceed the protocol buffer size of 64k
     * 
     * @return "Ok!" or an error message.
     * 
     * @throws
     * - ERR: expecting varname 
     * - ERR: expecting . 
     * - ERR: path too deep 
     * - ERR: name <varname> is unknown 
     * - ERR: bad pathlen 
     * - ERR: missing } 
     * - ERR: expecting value 
     * - ERR: bad resolution format 
     * - ERR: Expecting quoted string
     * 
    */
    {"set", 2, CMD_SET},
    /**
     * @brief Start Recording in a Cine (rec)
     * 
     * @paragraph The trigger command simulates a hardware trigger. The command does not check whether the currently active cine can be triggered. After the trigger command (or a hardware trigger) is received, the camera ac- quires ptframes frames in the current cine, marks it as stored (with the STR flag) then switches to the next non-preview cine in the table that has the RDY flag set. If none is found, it switches to the preview cine. If the camera is recording directly to a cinemag this command will stop the cinemag recording.
     * 
     * @overload rec <cine_number> 
     * @overload rec
     * 
     * @param cine_number The optional <cine_number> is the number of a cine from the cine table that is valid.
     * 
     * @return "Ok!" or an error message.
     * 
     * @throws
     * - ERR: an automatic operation is in progress 
     * - ERR: invalid cine number
     * 
    */
    {"rec", 1, CMD_START_RECORDING_IN_A_CINE},
    {"del", 1, CMD_DELETE_A_CINE},
    {"rel", 1, CMD_RELEASE_A_CINE},
    /**
     * @brief Software Trigger (trig)
     * 
     * @paragraph Start recording in the requested cine. If the specified cine contains a recording, it is silently deleted first. The acquisition parameters are copied from defc before the start of recording. When a rec command without arguments is received, if the current active cine is a preview cine and there is another cine in the ready state, the camera begins recording in the latter. If the camera is recording in a non-preview cine or if there is no ready cine available, a rec without argument has no effect.
     * 
     * @overload rec
     * 
     * @return "Ok!"
     * 
    */
    {"trig", 0, CMD_SOFTWARE_TRIGGER},
    {"cstats", 0, CMD_GET_CINE_STATES},
    /**
     * @brief Start Data Connection (startdata)
     *  
     * @paragraph This command tells the camera software to create a socket connection for trans- mission of image data and timestamps. The application software should create a socket and accept TCP connections on it, then issue a startdata command telling the camera software the port to connect to. Once the camera succesfully connects, the data stream is created. It is used to transfer bulk binary data to the application software. Only one data stream can be open at any given time. A new startdata or attach command will close the current data stream and open a new one. The data stream can be terminated by the application software by closing it’s end of the socket.
     * 
     * @overload startdata {port:<port_number>}
     * 
     * @param port_number The port number to which the camera will attempt to connect.
     * 
     * @return "Ok!" when the connection has been successfully established, or an error message.
     * 
     * @throws
     * - ERR: missing command args 
     * - ERR: missing argvalue for <argname> 
     * - ERR: Cannot start data conn 
     * - ERR: data transfer disabled 
     * - ERR: arg <argname> is mandatory for command <cmdname>
    */
    {"startdata", 1, CMD_START_DATA_CONNECTION},
    /**
     * @brief Start Data Connection (startdata)
     *  
     * @paragraph An application software that has already established a control stream may es- tablish a data stream by connecting a socket to the port 7116 on the camera and instructing the camera to use this new connection as data stream. In order to do that the application software must first connect a socket to the port 7116 on the camera, obtain the TCP port number for this socket and issue the attach port number command. Once this command completes, the data stream is created. It is used to transfer bulk binary data (images and time stamps) to the application software. Only one data stream can be open at any given time. A new startdata or attach command will close the current data stream and open a new one. The data stream can be terminated by the application software by closing it’s end of the socket.
     * 
     * @overload attach {port:<port_number>}
     * 
     * @param port_number The camera will attempt to attach the control connection on which this command is received to a data connection socket identified by the supplied port number.
     * 
     * @return "Ok!" when the connection has been successfully established, or an error message.
     * 
     * @throws
     * - ERR: cannot attach on serial lines 
     * - ERR: attach failure
    */
    {"attach", 1, CMD_ATTACH},
    /**
     * @brief Get Images (img)
     * 
     * @paragraph On the receipt of a img command the camera checks the supplied parameters and immediately generates a response. A data transfer request is placed in an internal queue. The actual data transfer (sending the binary image block on the data stream socket) begins after a short latency time. Other img or time commands can be issued before the data transfer is over. As img and time commands are processed, the respective data is sent over the data stream in the order of the requests. The data stream must be set up before img and time commands can be accepted. There is some latency in processing the img and time commands. For optimum peformance when dowloading large data sets, it is recommended that either large blocks of data are requested (e.g. several images) or that new img commands are issued while data is read from the data stream. To get specific images from a cine (as opposed to live images), the cine must be in the stored state. For cameras that have the earlyimg feature, it is possible to request images with numbers between c#.firstframe and c#.lastframe as soon as a cine is triggered. The image format tokens (8, 8R, etc.) have numerical equivalents, listed below. When the camera returns a format, it is generally in the numerical form. The img command in ph16 accepts the format in either token or numerical form. Although no error is generated, the total amount of data transferred by one img command should not exceed 2Gbytes.
     * 
     * @overload img {cine:<cine_number>, start:<first_frame>, cnt:<frame_count> [, fmt:<format>][, from:<image_source>]}
     * 
     * @param <cine_number> the cine from which images are taken; A value of -1 will return live images from the currently active cine. 
     * @param <first_frame> the number of the first frame to send (ignored if cine_number is -1). 
     * @param <frame_count> the number of frames to send. 
     * @param <format> an optional token or number setting the format in which the images should be sent. The format must be included in info.imgformats in order to be valid. The default value is 8. 
     * @param <image_source> an optional token or number indicating the source of data (ig- nored if cine_number is -1). Use ram or numeric 0 to read from a RAM cine (default). Use mag or numeric 1 to read from a cine mag cine.
     * 
     * @note \n
     *  | Token 	| Number 	| Description 	|
     *  |---	|---	|---	|
     *  | 8 	| 8 	| 8 bits per pixel, FPN and PRNU cor- rected, linear, raw 	|
     *  | 8R 	| -8 	| 8 bits per pixel, uncorrected, linear, raw 	|
     *  | P16 	| 272 	| 16 bits per pixel, FPN and PRNU cor- rected, linear, raw, little-endian. The range of values is 0-65535. 	|
     *  | P16R 	| -272 	| Same as P16 but uncorrected/ 	|
     *  | P10 	| 266 	| 10 bits per pixel packed into 32-bit big- endian words, FPN and PRNU corrected, non-linear, raw. 	|
     *  | P12L 	| 524 	| 12 bits per pixel packed into 32-bit big- endian words, FPN and PRNU corrected, linear, raw. 	|
     * 
     * @warning When requesting live images, one cannot always tell from which cine or at which resolution these will be taken. This happens because the active cine can change asynchronously as a result of a hardware event. Also, the resolution of a given cine can change between the moment one reads it with a get command and the moment when the img command is issued. When requesting images from a cine stored in RAM, the fmt field of the response replicates the corresponding command parameter. When requesting images from a cine stored to a cine mag, the format of the cine was frozen at save time, and the camera returns this format in the fmt field of the response. The img command processing is atomic, so it is guaranteed that the image data sent for a given request comes from the cine and at the resolution given in the response.
     * 
     * @return OK! { cine: <cine_number>, res:<res_x>x<res_y>, fmt: <format>}
     * 
     * @throws
     * - ERR: missing command args 
     * - ERR: invalid cine number 
     * - ERR: cine status invalid 
     * - ERR: no cinemag 
     * - ERR: start frame outside range 
     * - ERR: start+count frame outside range 
     * - ERR: count should be > 0 
     * - ERR: unsupported image format 
     * - ERR: data transfer disabled
    */
    {"img", 5, CMD_GET_IMAGES}, // 3 necesarry + 2 optional
    /**
     * @brief Get Images on 10G Ethernet or RTO(ximg)
     * 
     * @paragraph The operation is similar to the img command, except that the images data stream is sent to the 10G Ethernet interface or RTO. When using RTO, a destination mac address must still be supplied, but is not used. The data format for ximg is always P10 or P12L. There is a 10 second timeout for the execution of individual ximg commands. The frame count should be chosen so that the transfer can complete well within that interval. Although no error is generated, the total amount of data transferred by one img command should not exceed 2Gbytes.
     * 
     * @overload ximg {cine:<cine_number>, start:<first_frame>, cnt:<frame_count>, dest:<mac_address>, from:<image_source>}
     * 
     * @param <cine_number> cine from which images are taken. 
     * <first_frame> the number of the first frame to send. 
     * @param <frame_count> the number of frames to send. 
     * @param <dest> a string of 6 hex bytes representing the destination mac address. 
     * @param <image_source> an optional token or number indicating the source of data. Use ram or numeric 0 to read from a RAM cine (default). Use mag or numeric 1 to read from a cine mag cine. 
    */
    {"ximg", 5, CMD_GET_XIMAGES}, // 5 necesarry
    /**
     * @brief Get Timestamps (time)
     * 
     * @paragraph On receipt of a time command the camera checks the supplied parameters and immediately generates a response. A data trasfer request is placed in an internal queue. The actual data transfer (sending the binary timestamp block on the data stream socket) begins after a short latency time. Other img or time commands can be issued before the data transfer is over.
     * 
     * As img and time commands are processed, the respective data is sent over the data stream in the order of the requests. The data stream must be setup before img and time commands can be accepted.
     * 
     * There is some latency in processing the img and time commands. For optimum peformance when downloading large data sets, it is recommended that either large blocks of data are requested or that new time commands are issued while data is read from the data stream.
     * 
     * @overload img {cine:<cine_number>, start:<first_frame>, cnt:<frame_count> [, fmt:<format>][, from:<image_source>]}
     * 
     * @param <cine_number> cine from which time stamps are taken. The cine must be in the stored state. 
     * @param <first_frame> the number of the first frame for which timestamps are re- quested. 
     * @param <stamp_count> the number of timestamps to send. 
     * @param <image_source> an optional token or number indicating the source of data. Use ram or numeric 0 to read from a RAM cine (default). Use mag or numeric 1 to read from a cine mag cine.
     *  
     * @note
     * # Timestamp Format 
     * Timestamps can have four formats, depending on whether or not the range data input to the camera is enabled, and if the 32 bit exptime and frac extension is used or not. Timestamps mark the moment the shutter closes (end of the exposure) The format of the data as a “C” struct is:
     * ```c
     *  struct short_time_stamp{ //cam.tsformat = 0
     *      unsigned int csecs; // time from beginning of the year in 1/100 sec units
     *      unsigned short exptime; // exposure time in us
     *      unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event;[0]:lock
     *  };
     *  struct short_time_stamp32{ //cam.tsformat = 1
     *      unsigned int csecs; // time from beginning of the year in 1/100 sec units
     *      unsigned short exptime; // exposure time in us
     *      unsigned short frac; // bits[15..2]: fractions (us to 10000); b[1]:event; b[0]:lock
     *      unsigned short exptime32; // exposure time extension (1/65536 of a us)
     *      unsigned short frac32; // time stamp extension (1/65536 of a us)
     *  };
     *  struct long_time_stamp{ //cam.tsformat = 2
     *      unsigned int csecs; // time from beginning of the year in 1/100 sec units
     *      unsigned short exptime; // exposure time in us
     *      unsigned short frac; // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
     *      unsigned int range_d0; // first 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d1; // second 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d2; // third 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d3; // fourth 32bits received as rangedata, lsb first, big endian
     *  };
     *  struct long_time_stamp32{ //cam.tsformat = 3
     *      unsigned int csecs; // time from beginning of the year in 1/100 sec units
     *      unsigned short exptime; // exposure time in us
     *      unsigned short frac; // bits[15..2]: fractions (us to 10000); bit[1]:event; bit[0]:lock
     *      unsigned short exptime32; // exposure time extension (1/65536 of a us)
     *      unsigned short frac32; // time stamp extension (1/65536 of a us)
     *      unsigned int range_d0; // first 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d1; // second 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d2; // third 32bits received as rangedata, lsb first, big endian
     *      unsigned int range_d3; // fourth 32bits received as rangedata, lsb first, big endian
     *  };
     * ```
     * Ints are 32 bits long. Structures are stored in a packed, big endian format. Following the IRIG time format, time stamps cycle every year. To get the full date, one should add irig.yearbegin to the seconds from the timestamp. 
     * Bits 0 and 1 of timestamp.frac are flags showing whether the camera time- base is locked to an IRIG timecode source (if b0:lock = 0) or the event input of the camera was active when the exposure of the respective frame ended (if b1:event = 0).
     * To reconstruct all the timestamp information, one could use the following ex- pressions:
     * ```c
     *  // calculate the number of seconds since 1970
     * tv_sec = timestamp.csecs / 100 + irig.yearbegin; 
     * // microsecond offset
     * tv_usec = (timestamp.csecs % 100) * 10000 + timestamp.frac >> 2; 
     * // flags
     * locked = (timestamp.frac & 0x01) ? FALSE : TRUE;
     * event_active = (timestamp.frac & 0x02) ? FALSE : TRUE;
     * ```
     * 
     * @warning The response to the time command contains the information required to cor- rectly interpret the binary data block scheduled to be transmitted on the data stream. <count> is the number of timestamps that are scheduled to be sent, and <timestamp size> is the size (in bytes) of each time stamp as follows:
     *  | cam.tsformat 	| Size (bytes) 	| Description 	|
     *  |---	|---	|---	|
     *  | 0 	| 8 	| No range data, 16 bits exptime and frac 	|
     *  | 1 	| 12 	| No range data, 32 bit exptime and frac 	|
     *  | 2 	| 24 	| 16 bytes of range data, 16 bits exptime and frac. 	|
     *  | 3 	| 28 	| 16 bytes of range data, 32 bits exptime and frac. 	|
     * 
     * @return OK! {cine:<cine_number>, cnt:<count>, size:<timestamp_size>} 
     * 
     * @throws
     * - ERR: missing command args 
     * - ERR: missing argvalue for <argname> 
     * - ERR: arg <argname> is mandatory for command <cmdname> 
     * - ERR: invalid cine number 
     * - ERR: cine status invalid 
     * - ERR: no cinemag 
     * - ERR: data transfer disabled 
     * - ERR: count should be > 0 
     * - ERR: requested cine not stored 
     * - ERR: requested cine has invalid timebuf 
     * - ERR: requested frame range is invalid
    */
    {"time", 4, CMD_GET_TIMESTAMPS}, // 3 necessary + 1 optional 
    {"ifconfig", 0, CMD_SHOW_NETWORK_INTERFACE_CONFIGURATION},
    {"route", 0, CMD_SHOW_ROUTING_TABLE},
    {"partition", 1, CMD_PARTITION_CINE_MEMORY},
    {"wbal", 0, CMD_PERFORM_WHITE_BALANCE},
    {"bref", 1, CMD_PERFORM_BLACK_REFERENCE}, // 1 optional
    {"wupdate", 0, CMD_PRNU_CORRECTION_UPDATE},
    {"bupdate", 0, CMD_BLACK_REFERENCE_UPDATE},
    {"ferase", 0, CMD_FLASH_ERASE},
    {"fsave", 3, CMD_FLASH_SAVE}, // 1 necessary + 2 optional
    {"cfsave", 3, CMD_STORAGE_DEVICE_SAVE}, // 1 necessary + 2 optional
    /**
     * @brief Retrieve internal camera log (tail)
     * 
     * @paragraph The camera logs debugging messages into an internal circular buffer. This command allows the retrieval of “tail” end of the buffer.
     * 
     * @overload tail
     * 
     * @return "Ok!{\\r\\n debug messages}\\r\\n"
     * 
    */
    {"tail", 0, CMD_RETRIEVE_INTERNAL_CAMERA_LOG},
    {"vplay", 9, CMD_PLAY_FRAMES_ON_VIDEO_OUTPUT}, // 9 optional
    {"clean", 0, CMD_CHECK_FOR_VARIABLE_CHANGES}, 
    {"notify", 1, CMD_ENABLE_STATUS_CHANGE_NOTIFICATIONS},
    {"isave", 0, CMD_SAVE_FACTORY_DEFAULTS},
    {"iload", 2, CMD_LOAD_FACTORY_DEFAULTS}, // 2 optional
    {"usave", 2, CMD_SAVE_USER_SETTINGS}, // 1 necessary + 1 optional 
    {"uload", 1, CMD_LOAD_USER_SETTINGS},
    {"uerase", 1, CMD_ERASE_USER_SETTINGS},
    {"uls", 0, CMD_LIST_USER_SETTINGS},
    {"console", 1, CMD_DEBUG_CONSOLE_MODE},
    /**
     * @brief Set lens aperture (fstop)
     * 
     * @paragraph Set the lens aperture to the specified value. Aperture values are floating point numbers. When an aperture of zero is requested, the lens will go wide open. 
     * The fstop command without argument returns the current aperture setting of the lens.
     * 
     * @overload fstop { value: <aperture>} 
     * @overload fstop
     * 
     * @return 
     * - Ok!
     * - Ok! {fstop: value}
     * @throws
     * - ERR: no lens
     * - ERR: bad arg
    */
    {"fstop", 1, CMD_SET_LENS_APERTURE},
    /**
     * @brief Move focus (focus)
     * 
     * @paragraph Request the lens to move the focus ring by the specified number of incremental units (ticks). All focus moves are relative to the current focus position. Lenses may have a hysteresis of a couple of ticks when changing focus direction. Focus units (ticks) are of arbitrary size, and uncalibrated. The full range of focus of most lenses is of the order of 1000-3000 ticks. The command without argument returns the current focus state. Possible values are:
     *  | Focus state 	| Description 	|
     *  |---	|---	|
     *  | ok 	| Last focus change operation completed succesfully 	|
     *  | manual 	| Last focus change failed. Lens is set to manual focus 	|
     *  | limit 	| The focus adjustment has hit a mechanical stop during the last move command. 	|
     *  | progress 	| Focus change operation is in progress. 	|
     *  | unknown 	| No focus change operation was requested since powerup 	|
     * 
     * @overload focus { value:<focus change>}
     * @overload focus
     * 
     * @return 
     * - "Ok! {focus: limit} 
     * - "Ok! {focus: manual} 
     * - "Ok! {focus: ok}
     * - "Ok! {focus: progress} 
     * - "Ok! {focus: unknown} 
     * - "Ok!
     * 
     * @throws
     * - ERR: no lens
     * - ERR: bad arg
    */
    {"focus", 1, CMD_MOVE_FOCUS},
    {"lens", 0, CMD_ISSUE_LENS_MOUNT_COMMAND},
    {"baud", 1, CMD_CHANGE_SERIAL_LINE_BAUDE_RATE},
    {"calib", 1, CMD_CAMERA_CALIBRATION},
    {"sysmon", 2, CMD_SYSTEM_MONITOR},
    {"testimg", 1, CMD_GENERATE_TEST_IMAGE},
    {"setrtc", 1, CMD_SET_REAL_TIME_CLOCK},
    {"cfls", 0, CMD_LIST_FILES_ON_STORAGE_DEVICE},
    {"cfrm", 1, CMD_REMOVE_FILE_FROM_STORAGE_DEVICE},
    {"cfformat", 0, CMD_FORMATE_STORAGE_DEVICE},
    {"cfread", 3, CMD_FILE_READ_DATA_FROM_STORAGE_DEVICE},
    {"preset", 4, CMD_USE_COLOR_PRESET}, // 4 optional
    {"mmset", 3, CMD_SET_MULTI_MATRIX_AXIS},
    {NULL,}
};

#endif // COMMANDS_H