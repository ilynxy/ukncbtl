#ifndef FLOPPY_HPP
#define FLOPPY_HPP

#include <stdint.h>
#include <stdio.h>
#include <windows.h>

//////////////////////////////////////////////////////////////////////
// CFloppy
#define FLOPPY_FSM_IDLE                 0

#define FLOPPY_CMD_CORRECTION250             04
#define FLOPPY_CMD_CORRECTION500            010
#define FLOPPY_CMD_ENGINESTART              020  ///< Engine on/off
#define FLOPPY_CMD_SIDEUP                   040  ///< Side: 1 -- upper head, 0 -- lower head
#define FLOPPY_CMD_DIR                     0100  ///< Direction: 0 -- to center (towards trk0), 1 -- from center (towards trk80)
#define FLOPPY_CMD_STEP                    0200  ///< Step / ready
#define FLOPPY_CMD_SEARCHSYNC              0400  ///< Search sync
#define FLOPPY_CMD_SKIPSYNC               01000  ///< Skip sync

#define FLOPPY_STATUS_TRACK0                 01  ///< Track 0 flag
#define FLOPPY_STATUS_RDY                    02  ///< Ready status
#define FLOPPY_STATUS_WRITEPROTECT           04  ///< Write protect
#define FLOPPY_STATUS_MOREDATA             0200  ///< Need more data flag
#define FLOPPY_STATUS_CHECKSUMOK         040000  ///< Checksum verified OK
#define FLOPPY_STATUS_INDEXMARK         0100000  ///< Index flag, indicates the beginning of track

#define FLOPPY_RAWTRACKSIZE             6250     ///< Raw track size, bytes
#define FLOPPY_RAWMARKERSIZE            (FLOPPY_RAWTRACKSIZE / 2)
#define FLOPPY_INDEXLENGTH              150      ///< Length of index hole, in bytes of raw track image

/// \brief Floppy drive (one of four drives in the floppy controller)
/// \sa CFloppyController
struct CFloppyDrive
{
    FILE* fpFile;       ///< File pointer of the disk image file
    bool okNetRT11Image;  ///< true - .rtd image, false - .dsk image
    bool okReadOnly;    ///< Write protection flag
    uint16_t track;         ///< Track number: from 0 to 79
    uint16_t side;          ///< Disk side: 0 or 1
    uint16_t dataptr;       ///< Data offset within m_data - "head" position
    uint16_t datatrack;     ///< Track number of data in m_data array
    uint16_t dataside;      ///< Disk side of data in m_data array
    uint8_t data[FLOPPY_RAWTRACKSIZE];  ///< Raw track image for the current track
    uint8_t marker[FLOPPY_RAWMARKERSIZE];  ///< Marker positions

public:
    CFloppyDrive();
    void Reset();       ///< Reset the device
};

/// \brief UKNC floppy controller (MZ standard)
/// \sa CFloppyDrive
class CFloppyController
{
protected:
    CFloppyDrive m_drivedata[4];  ///< Floppy drives
    CFloppyDrive* m_pDrive;  ///< Current drive
    uint16_t m_drive;       ///< Current drive number: from 0 to 3
    uint16_t m_track;       ///< Track number: from 0 to 79
    uint16_t m_side;        ///< Disk side: 0 or 1
    uint16_t m_status;      ///< See FLOPPY_STATUS_XXX defines
    uint16_t m_flags;       ///< See FLOPPY_CMD_XXX defines
    uint16_t m_datareg;     ///< Read mode data register
    uint16_t m_writereg;    ///< Write mode data register
    bool m_writeflag;   ///< Write mode data register has data
    bool m_writemarker; ///< Write marker in m_marker
    uint16_t m_shiftreg;    ///< Write mode shift register
    bool m_shiftflag;   ///< Write mode shift register has data
    bool m_shiftmarker; ///< Write marker in m_marker
    bool m_writing;     ///< true = write mode, false = read mode
    bool m_searchsync;  ///< Read sub-mode: true = search for sync, false = just read
    bool m_crccalculus; ///< true = CRC is calculated now
    bool m_trackchanged;  ///< true = m_data was changed - need to save it into the file
    bool m_okTrace;         ///< Trace mode on/off

public:
    CFloppyController();
    ~CFloppyController();
    void Reset();       ///< Reset the device

public:
    /// \brief Attach the image to the drive -- insert disk
    bool AttachImage(int drive, LPCTSTR sFileName);
    /// \brief Detach image from the drive -- remove disk
    void DetachImage(int drive);
    /// \brief Check if the drive has an image attached
    bool IsAttached(int drive) const { return (m_drivedata[drive].fpFile != nullptr); }
    /// \brief Check if the drive's attached image is read-only
    bool IsReadOnly(int drive) const { return m_drivedata[drive].okReadOnly; }
    /// \brief Check if floppy engine now rotates
    bool IsEngineOn() const { return (m_flags & FLOPPY_CMD_ENGINESTART) != 0; }
    uint16_t GetData(void);         ///< Reading port 177132 -- data
    uint16_t GetState(void);        ///< Reading port 177130 -- device status
    void SetCommand(uint16_t cmd);  ///< Writing to port 177130 -- commands
    void WriteData(uint16_t data);  ///< Writing to port 177132 -- data
    void Periodic();            ///< Rotate disk; call it each 64 us -- 15625 times per second
    void SetTrace(bool okTrace) { m_okTrace = okTrace; }  // Set trace mode on/off

private:
    void PrepareTrack();
    void FlushChanges();  ///< If current track was changed - save it
};


#endif // FLOPPY_HPP
