#ifndef HARD_HPP
#define HARD_HPP

#include <stdint.h>
#include <stdio.h>
#include <windows.h>

//////////////////////////////////////////////////////////////////////
// CHardDrive

#define IDE_DISK_SECTOR_SIZE      512

/// \brief UKNC IDE hard drive
class CHardDrive
{
protected:
    FILE*   m_fpFile;           ///< File pointer for the attached HDD image
    bool    m_okReadOnly;       ///< Flag indicating that the HDD image file is read-only
    bool    m_okInverted;       ///< Flag indicating that the HDD image has inverted bits
    uint8_t m_status;           ///< IDE status register, see IDE_STATUS_XXX constants
    uint8_t m_error;            ///< IDE error register, see IDE_ERROR_XXX constants
    uint8_t m_command;          ///< Current IDE command, see IDE_COMMAND_XXX constants
    int     m_numcylinders;     ///< Cylinder count
    int     m_numheads;         ///< Head count
    int     m_numsectors;       ///< Sectors per track
    int     m_curcylinder;      ///< Current cylinder number
    int     m_curhead;          ///< Current head number
    int     m_cursector;        ///< Current sector number
    int     m_curheadreg;       ///< Current head number
    int     m_sectorcount;      ///< Sector counter for read/write operations
    uint8_t m_buffer[IDE_DISK_SECTOR_SIZE];  ///< Sector data buffer
    int     m_bufferoffset;     ///< Current offset within sector: 0..511
    int     m_timeoutcount;     ///< Timeout counter to wait for the next event
    int     m_timeoutevent;     ///< Current stage of operation, see TimeoutEvent enum

public:
    CHardDrive();
    ~CHardDrive();
    /// \brief Reset the device.
    void Reset();
    /// \brief Attach HDD image file to the device
    bool AttachImage(LPCTSTR sFileName);
    /// \brief Detach HDD image file from the device
    void DetachImage();
    /// \brief Check if the attached hard drive image is read-only
    bool IsReadOnly() const { return m_okReadOnly; }

public:
    /// \brief Read word from the device port
    uint16_t ReadPort(uint16_t port);
    /// \brief Write word th the device port
    void WritePort(uint16_t port, uint16_t data);
    /// \brief Rotate disk
    void Periodic();

private:
    uint32_t CalculateOffset() const;  ///< Calculate sector offset in the HDD image
    void HandleCommand(uint8_t command);  ///< Handle the IDE command
    void ReadNextSector();
    void ReadSectorDone();
    void WriteSectorDone();
    void NextSector();          ///< Advance to the next sector, CHS-based
    void ContinueRead();
    void ContinueWrite();
    void IdentifyDrive();       ///< Prepare m_buffer for the IDENTIFY DRIVE command
};

#endif // HARD_HPP
