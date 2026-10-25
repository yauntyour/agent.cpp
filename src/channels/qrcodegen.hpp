#pragma once
#include "core/common.hpp"

namespace qrcodegen
{

    /*
     * A segment of character/binary/control data in a QR Code symbol.
     * Instances of this class are immutable.
     * The mid-level way to create a segment is to take the payload data
     * and call a static factory function such as QrSegment::makeNumeric().
     * The low-level way to create a segment is to custom-make the bit buffer
     * and call the QrSegment() constructor with appropriate values.
     * This segment class imposes no length restrictions, but QR Codes have restrictions.
     * Even in the most favorable conditions, a QR Code can only hold 7089 characters of data.
     * Any segment longer than this is meaningless for the purpose of generating QR Codes.
     */
    class QrSegment final
    {

        /*---- Public helper enumeration ----*/

        /*
         * Describes how a segment's data bits are interpreted. Immutable.
         */
    public:
        class Mode final
        {

            /*-- Constants --*/

        public:
            static const Mode NUMERIC;

        public:
            static const Mode ALPHANUMERIC;

        public:
            static const Mode BYTE;

        public:
            static const Mode KANJI;

        public:
            static const Mode ECI;

            /*-- Fields --*/

            // The mode indicator bits, which is a uint4 value (range 0 to 15).
        private:
            int modeBits;

            // Number of character count bits for three different version ranges.
        private:
            int numBitsCharCount[3];

            /*-- Constructor --*/

        private:
            Mode(int mode, int cc0, int cc1, int cc2);

            /*-- Methods --*/

            /*
             * (Package-private) Returns the mode indicator bits, which is an unsigned 4-bit value (range 0 to 15).
             */
        public:
            int getModeBits() const;

            /*
             * (Package-private) Returns the bit width of the character count field for a segment in
             * this mode in a QR Code at the given version number. The result is in the range [0, 16].
             */
        public:
            int numCharCountBits(int ver) const;
        };

        /*---- Static factory functions (mid level) ----*/

        /*
         * Returns a segment representing the given binary data encoded in
         * byte mode. All input byte vectors are acceptable. Any text string
         * can be converted to UTF-8 bytes and encoded as a byte mode segment.
         */
    public:
        static QrSegment makeBytes(const std::vector<std::uint8_t> &data);

        /*
         * Returns a segment representing the given string of decimal digits encoded in numeric mode.
         */
    public:
        static QrSegment makeNumeric(const char *digits);

        /*
         * Returns a segment representing the given text string encoded in alphanumeric mode.
         * The characters allowed are: 0 to 9, A to Z (uppercase only), space,
         * dollar, percent, asterisk, plus, hyphen, period, slash, colon.
         */
    public:
        static QrSegment makeAlphanumeric(const char *text);

        /*
         * Returns a list of zero or more segments to represent the given text string. The result
         * may use various segment modes and switch modes to optimize the length of the bit stream.
         */
    public:
        static std::vector<QrSegment> makeSegments(const char *text);

        /*
         * Returns a segment representing an Extended Channel Interpretation
         * (ECI) designator with the given assignment value.
         */
    public:
        static QrSegment makeEci(long assignVal);

        /*---- Public static helper functions ----*/

        /*
         * Tests whether the given string can be encoded as a segment in numeric mode.
         * A string is encodable iff each character is in the range 0 to 9.
         */
    public:
        static bool isNumeric(const char *text);

        /*
         * Tests whether the given string can be encoded as a segment in alphanumeric mode.
         * A string is encodable iff each character is in the following set: 0 to 9, A to Z
         * (uppercase only), space, dollar, percent, asterisk, plus, hyphen, period, slash, colon.
         */
    public:
        static bool isAlphanumeric(const char *text);

        /*---- Instance fields ----*/

        /* The mode indicator of this segment. Accessed through getMode(). */
    private:
        const Mode *mode;

        /* The length of this segment's unencoded data. Measured in characters for
         * numeric/alphanumeric/kanji mode, bytes for byte mode, and 0 for ECI mode.
         * Always zero or positive. Not the same as the data's bit length.
         * Accessed through getNumChars(). */
    private:
        int numChars;

        /* The data bits of this segment. Accessed through getData(). */
    private:
        std::vector<bool> data;

        /*---- Constructors (low level) ----*/

        /*
         * Creates a new QR Code segment with the given attributes and data.
         * The character count (numCh) must agree with the mode and the bit buffer length,
         * but the constraint isn't checked. The given bit buffer is copied and stored.
         */
    public:
        QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt);

        /*
         * Creates a new QR Code segment with the given parameters and data.
         * The character count (numCh) must agree with the mode and the bit buffer length,
         * but the constraint isn't checked. The given bit buffer is moved and stored.
         */
    public:
        QrSegment(const Mode &md, int numCh, std::vector<bool> &&dt);

        /*---- Methods ----*/

        /*
         * Returns the mode field of this segment.
         */
    public:
        const Mode &getMode() const;

        /*
         * Returns the character count field of this segment.
         */
    public:
        int getNumChars() const;

        /*
         * Returns the data bits of this segment.
         */
    public:
        const std::vector<bool> &getData() const;

        // (Package-private) Calculates the number of bits needed to encode the given segments at
        // the given version. Returns a non-negative number if successful. Otherwise returns -1 if a
        // segment has too many characters to fit its length field, or the total bits exceeds INT_MAX.
    public:
        static int getTotalBits(const std::vector<QrSegment> &segs, int version);

        /*---- Private constant ----*/

        /* The set of all legal characters in alphanumeric mode, where
         * each character value maps to the index in the string. */
    private:
        static const char *ALPHANUMERIC_CHARSET;
    };

    /*
     * A QR Code symbol, which is a type of two-dimension barcode.
     * Invented by Denso Wave and described in the ISO/IEC 18004 standard.
     * Instances of this class represent an immutable square grid of dark and light cells.
     * The class provides static factory functions to create a QR Code from text or binary data.
     * The class covers the QR Code Model 2 specification, supporting all versions (sizes)
     * from 1 to 40, all 4 error correction levels, and 4 character encoding modes.
     *
     * Ways to create a QR Code object:
     * - High level: Take the payload data and call QrCode::encodeText() or QrCode::encodeBinary().
     * - Mid level: Custom-make the list of segments and call QrCode::encodeSegments().
     * - Low level: Custom-make the array of data codeword bytes (including
     *   segment headers and final padding, excluding error correction codewords),
     *   supply the appropriate version number, and call the QrCode() constructor.
     * (Note that all ways require supplying the desired error correction level.)
     */
    class QrCode final
    {

        /*---- Public helper enumeration ----*/

        /*
         * The error correction level in a QR Code symbol.
         */
    public:
        enum class Ecc
        {
            LOW = 0,  // The QR Code can tolerate about  7% erroneous codewords
            MEDIUM,   // The QR Code can tolerate about 15% erroneous codewords
            QUARTILE, // The QR Code can tolerate about 25% erroneous codewords
            HIGH,     // The QR Code can tolerate about 30% erroneous codewords
        };

        // Returns a value in the range 0 to 3 (unsigned 2-bit integer).
    private:
        static int getFormatBits(Ecc ecl);

        /*---- Static factory functions (high level) ----*/

        /*
         * Returns a QR Code representing the given Unicode text string at the given error correction level.
         * As a conservative upper bound, this function is guaranteed to succeed for strings that have 2953 or fewer
         * UTF-8 code units (not Unicode code points) if the low error correction level is used. The smallest possible
         * QR Code version is automatically chosen for the output. The ECC level of the result may be higher than
         * the ecl argument if it can be done without increasing the version.
         */
    public:
        static QrCode encodeText(const char *text, Ecc ecl);

        /*
         * Returns a QR Code representing the given binary data at the given error correction level.
         * This function always encodes using the binary segment mode, not any text mode. The maximum number of
         * bytes allowed is 2953. The smallest possible QR Code version is automatically chosen for the output.
         * The ECC level of the result may be higher than the ecl argument if it can be done without increasing the version.
         */
    public:
        static QrCode encodeBinary(const std::vector<std::uint8_t> &data, Ecc ecl);

        /*---- Static factory functions (mid level) ----*/

        /*
         * Returns a QR Code representing the given segments with the given encoding parameters.
         * The smallest possible QR Code version within the given range is automatically
         * chosen for the output. Iff boostEcl is true, then the ECC level of the result
         * may be higher than the ecl argument if it can be done without increasing the
         * version. The mask number is either between 0 to 7 (inclusive) to force that
         * mask, or -1 to automatically choose an appropriate mask (which may be slow).
         * This function allows the user to create a custom sequence of segments that switches
         * between modes (such as alphanumeric and byte) to encode text in less space.
         * This is a mid-level API; the high-level API is encodeText() and encodeBinary().
         */
    public:
        static QrCode encodeSegments(const std::vector<QrSegment> &segs, Ecc ecl,
                                     int minVersion = 1, int maxVersion = 40, int mask = -1, bool boostEcl = true); // All optional parameters

        /*---- Instance fields ----*/

        // Immutable scalar parameters:

        /* The version number of this QR Code, which is between 1 and 40 (inclusive).
         * This determines the size of this barcode. */
    private:
        int version;

        /* The width and height of this QR Code, measured in modules, between
         * 21 and 177 (inclusive). This is equal to version * 4 + 17. */
    private:
        int size;

        /* The error correction level used in this QR Code. */
    private:
        Ecc errorCorrectionLevel;

        /* The index of the mask pattern used in this QR Code, which is between 0 and 7 (inclusive).
         * Even if a QR Code is created with automatic masking requested (mask = -1),
         * the resulting object still has a mask value between 0 and 7. */
    private:
        int mask;

        // Private grids of modules/pixels, with dimensions of size*size:

        // The modules of this QR Code (false = light, true = dark).
        // Immutable after constructor finishes. Accessed through getModule().
    private:
        std::vector<std::vector<bool>> modules;

        // Indicates function modules that are not subjected to masking. Discarded when constructor finishes.
    private:
        std::vector<std::vector<bool>> isFunction;

        /*---- Constructor (low level) ----*/

        /*
         * Creates a new QR Code with the given version number,
         * error correction level, data codeword bytes, and mask number.
         * This is a low-level API that most users should not use directly.
         * A mid-level API is the encodeSegments() function.
         */
    public:
        QrCode(int ver, Ecc ecl, const std::vector<std::uint8_t> &dataCodewords, int msk);

        /*---- Public instance methods ----*/

        /*
         * Returns this QR Code's version, in the range [1, 40].
         */
    public:
        int getVersion() const;

        /*
         * Returns this QR Code's size, in the range [21, 177].
         */
    public:
        int getSize() const;

        /*
         * Returns this QR Code's error correction level.
         */
    public:
        Ecc getErrorCorrectionLevel() const;

        /*
         * Returns this QR Code's mask, in the range [0, 7].
         */
    public:
        int getMask() const;

        /*
         * Returns the color of the module (pixel) at the given coordinates, which is false
         * for light or true for dark. The top left corner has the coordinates (x=0, y=0).
         * If the given coordinates are out of bounds, then false (light) is returned.
         */
    public:
        bool getModule(int x, int y) const;

        /*---- Private helper methods for constructor: Drawing function modules ----*/

        // Reads this object's version field, and draws and marks all function modules.
    private:
        void drawFunctionPatterns();

        // Draws two copies of the format bits (with its own error correction code)
        // based on the given mask and this object's error correction level field.
    private:
        void drawFormatBits(int msk);

        // Draws two copies of the version bits (with its own error correction code),
        // based on this object's version field, iff 7 <= version <= 40.
    private:
        void drawVersion();

        // Draws a 9*9 finder pattern including the border separator,
        // with the center module at (x, y). Modules can be out of bounds.
    private:
        void drawFinderPattern(int x, int y);

        // Draws a 5*5 alignment pattern, with the center module
        // at (x, y). All modules must be in bounds.
    private:
        void drawAlignmentPattern(int x, int y);

        // Sets the color of a module and marks it as a function module.
        // Only used by the constructor. Coordinates must be in bounds.
    private:
        void setFunctionModule(int x, int y, bool isDark);

        // Returns the color of the module at the given coordinates, which must be in range.
    private:
        bool module(int x, int y) const;

        /*---- Private helper methods for constructor: Codewords and masking ----*/

        // Returns a new byte string representing the given data with the appropriate error correction
        // codewords appended to it, based on this object's version and error correction level.
    private:
        std::vector<std::uint8_t> addEccAndInterleave(const std::vector<std::uint8_t> &data) const;

        // Draws the given sequence of 8-bit codewords (data and error correction) onto the entire
        // data area of this QR Code. Function modules need to be marked off before this is called.
    private:
        void drawCodewords(const std::vector<std::uint8_t> &data);

        // XORs the codeword modules in this QR Code with the given mask pattern.
        // The function modules must be marked and the codeword bits must be drawn
        // before masking. Due to the arithmetic of XOR, calling applyMask() with
        // the same mask value a second time will undo the mask. A final well-formed
        // QR Code needs exactly one (not zero, two, etc.) mask applied.
    private:
        void applyMask(int msk);

        // Calculates and returns the penalty score based on state of this QR Code's current modules.
        // This is used by the automatic mask choice algorithm to find the mask pattern that yields the lowest score.
    private:
        long getPenaltyScore() const;

        /*---- Private helper functions ----*/

        // Returns an ascending list of positions of alignment patterns for this version number.
        // Each position is in the range [0,177), and are used on both the x and y axes.
        // This could be implemented as lookup table of 40 variable-length lists of unsigned bytes.
    private:
        std::vector<int> getAlignmentPatternPositions() const;

        // Returns the number of data bits that can be stored in a QR Code of the given version number, after
        // all function modules are excluded. This includes remainder bits, so it might not be a multiple of 8.
        // The result is in the range [208, 29648]. This could be implemented as a 40-entry lookup table.
    private:
        static int getNumRawDataModules(int ver);

        // Returns the number of 8-bit data (i.e. not error correction) codewords contained in any
        // QR Code of the given version number and error correction level, with remainder bits discarded.
        // This stateless pure function could be implemented as a (40*4)-cell lookup table.
    private:
        static int getNumDataCodewords(int ver, Ecc ecl);

        // Returns a Reed-Solomon ECC generator polynomial for the given degree. This could be
        // implemented as a lookup table over all possible parameter values, instead of as an algorithm.
    private:
        static std::vector<std::uint8_t> reedSolomonComputeDivisor(int degree);

        // Returns the Reed-Solomon error correction codeword for the given data and divisor polynomials.
    private:
        static std::vector<std::uint8_t> reedSolomonComputeRemainder(const std::vector<std::uint8_t> &data, const std::vector<std::uint8_t> &divisor);

        // Returns the product of the two given field elements modulo GF(2^8/0x11D).
        // All inputs are valid. This could be implemented as a 256*256 lookup table.
    private:
        static std::uint8_t reedSolomonMultiply(std::uint8_t x, std::uint8_t y);

        // Can only be called immediately after a light run is added, and
        // returns either 0, 1, or 2. A helper function for getPenaltyScore().
    private:
        int finderPenaltyCountPatterns(const std::array<int, 7> &runHistory) const;

        // Must be called at the end of a line (row or column) of modules. A helper function for getPenaltyScore().
    private:
        int finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory) const;

        // Pushes the given value to the front and drops the last value. A helper function for getPenaltyScore().
    private:
        void finderPenaltyAddHistory(int currentRunLength, std::array<int, 7> &runHistory) const;

        // Returns true iff the i'th bit of x is set to 1.
    private:
        static bool getBit(long x, int i);

        /*---- Constants and tables ----*/

        // The minimum version number supported in the QR Code Model 2 standard.
    public:
        static constexpr int MIN_VERSION = 1;

        // The maximum version number supported in the QR Code Model 2 standard.
    public:
        static constexpr int MAX_VERSION = 40;

        // For use in getPenaltyScore(), when evaluating which mask is best.
    private:
        static const int PENALTY_N1;

    private:
        static const int PENALTY_N2;

    private:
        static const int PENALTY_N3;

    private:
        static const int PENALTY_N4;

    private:
        static const std::int8_t ECC_CODEWORDS_PER_BLOCK[4][41];

    private:
        static const std::int8_t NUM_ERROR_CORRECTION_BLOCKS[4][41];
    };

    /*---- Public exception class ----*/

    /*
     * Thrown when the supplied data does not fit any QR Code version. Ways to handle this exception include:
     * - Decrease the error correction level if it was greater than Ecc::LOW.
     * - If the encodeSegments() function was called with a maxVersion argument, then increase
     *   it if it was less than QrCode::MAX_VERSION. (This advice does not apply to the other
     *   factory functions because they search all versions up to QrCode::MAX_VERSION.)
     * - Split the text data into better or optimal segments in order to reduce the number of bits required.
     * - Change the text or binary data to be shorter.
     * - Change the text to fit the character set of a particular segment mode (e.g. alphanumeric).
     * - Propagate the error upward to the caller/user.
     */
    class data_too_long : public std::length_error
    {

    public:
        explicit data_too_long(const std::string &msg);
    };

    /*
     * An appendable sequence of bits (0s and 1s). Mainly used by QrSegment.
     */
    class BitBuffer final : public std::vector<bool>
    {

        /*---- Constructor ----*/

        // Creates an empty bit buffer (length 0).
    public:
        BitBuffer();

        /*---- Method ----*/

        // Appends the given number of low-order bits of the given value
        // to this buffer. Requires 0 <= len <= 31 and val < 2^len.
    public:
        void appendBits(std::uint32_t val, int len);
    };

}

using std::int8_t;
using std::size_t;
using std::uint8_t;
using std::vector;

namespace qrcodegen
{

    /*---- Class QrSegment ----*/

    inline QrSegment::Mode::Mode(int mode, int cc0, int cc1, int cc2) : modeBits(mode)
    {
        numBitsCharCount[0] = cc0;
        numBitsCharCount[1] = cc1;
        numBitsCharCount[2] = cc2;
    }

    inline int QrSegment::Mode::getModeBits() const
    {
        return modeBits;
    }

    inline int QrSegment::Mode::numCharCountBits(int ver) const
    {
        return numBitsCharCount[(ver + 7) / 17];
    }

    inline const QrSegment::Mode QrSegment::Mode::NUMERIC(0x1, 10, 12, 14);
    inline const QrSegment::Mode QrSegment::Mode::ALPHANUMERIC(0x2, 9, 11, 13);
    inline const QrSegment::Mode QrSegment::Mode::BYTE(0x4, 8, 16, 16);
    inline const QrSegment::Mode QrSegment::Mode::KANJI(0x8, 8, 10, 12);
    inline const QrSegment::Mode QrSegment::Mode::ECI(0x7, 0, 0, 0);

    inline QrSegment QrSegment::makeBytes(const vector<uint8_t> &data)
    {
        if (data.size() > static_cast<unsigned int>(INT_MAX))
            throw std::length_error("Data too long");
        BitBuffer bb;
        for (uint8_t b : data)
            bb.appendBits(b, 8);
        return QrSegment(Mode::BYTE, static_cast<int>(data.size()), std::move(bb));
    }

    inline QrSegment QrSegment::makeNumeric(const char *digits)
    {
        BitBuffer bb;
        int accumData = 0;
        int accumCount = 0;
        int charCount = 0;
        for (; *digits != '\0'; digits++, charCount++)
        {
            char c = *digits;
            if (c < '0' || c > '9')
                throw std::domain_error("String contains non-numeric characters");
            accumData = accumData * 10 + (c - '0');
            accumCount++;
            if (accumCount == 3)
            {
                bb.appendBits(static_cast<uint32_t>(accumData), 10);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) // 1 or 2 digits remaining
            bb.appendBits(static_cast<uint32_t>(accumData), accumCount * 3 + 1);
        return QrSegment(Mode::NUMERIC, charCount, std::move(bb));
    }

    inline QrSegment QrSegment::makeAlphanumeric(const char *text)
    {
        BitBuffer bb;
        int accumData = 0;
        int accumCount = 0;
        int charCount = 0;
        for (; *text != '\0'; text++, charCount++)
        {
            const char *temp = std::strchr(ALPHANUMERIC_CHARSET, *text);
            if (temp == nullptr)
                throw std::domain_error("String contains unencodable characters in alphanumeric mode");
            accumData = accumData * 45 + static_cast<int>(temp - ALPHANUMERIC_CHARSET);
            accumCount++;
            if (accumCount == 2)
            {
                bb.appendBits(static_cast<uint32_t>(accumData), 11);
                accumData = 0;
                accumCount = 0;
            }
        }
        if (accumCount > 0) // 1 character remaining
            bb.appendBits(static_cast<uint32_t>(accumData), 6);
        return QrSegment(Mode::ALPHANUMERIC, charCount, std::move(bb));
    }

    inline vector<QrSegment> QrSegment::makeSegments(const char *text)
    {
        // Select the most efficient segment encoding automatically
        vector<QrSegment> result;
        if (*text == '\0')
            ; // Leave result empty
        else if (isNumeric(text))
            result.push_back(makeNumeric(text));
        else if (isAlphanumeric(text))
            result.push_back(makeAlphanumeric(text));
        else
        {
            vector<uint8_t> bytes;
            for (; *text != '\0'; text++)
                bytes.push_back(static_cast<uint8_t>(*text));
            result.push_back(makeBytes(bytes));
        }
        return result;
    }

    inline QrSegment QrSegment::makeEci(long assignVal)
    {
        BitBuffer bb;
        if (assignVal < 0)
            throw std::domain_error("ECI assignment value out of range");
        else if (assignVal < (1 << 7))
            bb.appendBits(static_cast<uint32_t>(assignVal), 8);
        else if (assignVal < (1 << 14))
        {
            bb.appendBits(2, 2);
            bb.appendBits(static_cast<uint32_t>(assignVal), 14);
        }
        else if (assignVal < 1000000L)
        {
            bb.appendBits(6, 3);
            bb.appendBits(static_cast<uint32_t>(assignVal), 21);
        }
        else
            throw std::domain_error("ECI assignment value out of range");
        return QrSegment(Mode::ECI, 0, std::move(bb));
    }

    inline QrSegment::QrSegment(const Mode &md, int numCh, const std::vector<bool> &dt) : mode(&md),
                                                                                   numChars(numCh),
                                                                                   data(dt)
    {
        if (numCh < 0)
            throw std::domain_error("Invalid value");
    }

    inline QrSegment::QrSegment(const Mode &md, int numCh, std::vector<bool> &&dt) : mode(&md),
                                                                              numChars(numCh),
                                                                              data(std::move(dt))
    {
        if (numCh < 0)
            throw std::domain_error("Invalid value");
    }

    inline int QrSegment::getTotalBits(const vector<QrSegment> &segs, int version)
    {
        int result = 0;
        for (const QrSegment &seg : segs)
        {
            int ccbits = seg.mode->numCharCountBits(version);
            if (seg.numChars >= (1L << ccbits))
                return -1; // The segment's length doesn't fit the field's bit width
            if (4 + ccbits > INT_MAX - result)
                return -1; // The sum will overflow an int type
            result += 4 + ccbits;
            if (seg.data.size() > static_cast<unsigned int>(INT_MAX - result))
                return -1; // The sum will overflow an int type
            result += static_cast<int>(seg.data.size());
        }
        return result;
    }

    inline bool QrSegment::isNumeric(const char *text)
    {
        for (; *text != '\0'; text++)
        {
            char c = *text;
            if (c < '0' || c > '9')
                return false;
        }
        return true;
    }

    inline bool QrSegment::isAlphanumeric(const char *text)
    {
        for (; *text != '\0'; text++)
        {
            if (std::strchr(ALPHANUMERIC_CHARSET, *text) == nullptr)
                return false;
        }
        return true;
    }

    inline const QrSegment::Mode &QrSegment::getMode() const
    {
        return *mode;
    }

    inline int QrSegment::getNumChars() const
    {
        return numChars;
    }

    inline const std::vector<bool> &QrSegment::getData() const
    {
        return data;
    }

    inline const char *QrSegment::ALPHANUMERIC_CHARSET = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";

    /*---- Class QrCode ----*/

    inline int QrCode::getFormatBits(Ecc ecl)
    {
        switch (ecl)
        {
        case Ecc::LOW:
            return 1;
        case Ecc::MEDIUM:
            return 0;
        case Ecc::QUARTILE:
            return 3;
        case Ecc::HIGH:
            return 2;
        default:
            throw std::logic_error("Unreachable");
        }
    }

    inline QrCode QrCode::encodeText(const char *text, Ecc ecl)
    {
        vector<QrSegment> segs = QrSegment::makeSegments(text);
        return encodeSegments(segs, ecl);
    }

    inline QrCode QrCode::encodeBinary(const vector<uint8_t> &data, Ecc ecl)
    {
        vector<QrSegment> segs{QrSegment::makeBytes(data)};
        return encodeSegments(segs, ecl);
    }

    inline QrCode QrCode::encodeSegments(const vector<QrSegment> &segs, Ecc ecl,
                                  int minVersion, int maxVersion, int mask, bool boostEcl)
    {
        if (!(MIN_VERSION <= minVersion && minVersion <= maxVersion && maxVersion <= MAX_VERSION) || mask < -1 || mask > 7)
            throw std::invalid_argument("Invalid value");

        // Find the minimal version number to use
        int version, dataUsedBits;
        for (version = minVersion;; version++)
        {
            int dataCapacityBits = getNumDataCodewords(version, ecl) * 8; // Number of data bits available
            dataUsedBits = QrSegment::getTotalBits(segs, version);
            if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
                break; // This version number is found to be suitable
            if (version >= maxVersion)
            { // All versions in the range could not fit the given data
                std::ostringstream sb;
                if (dataUsedBits == -1)
                    sb << "Segment too long";
                else
                {
                    sb << "Data length = " << dataUsedBits << " bits, ";
                    sb << "Max capacity = " << dataCapacityBits << " bits";
                }
                throw data_too_long(sb.str());
            }
        }
        assert(dataUsedBits != -1);

        // Increase the error correction level while the data still fits in the current version number
        for (Ecc newEcl : {Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH})
        { // From low to high
            if (boostEcl && dataUsedBits <= getNumDataCodewords(version, newEcl) * 8)
                ecl = newEcl;
        }

        // Concatenate all segments to create the data bit string
        BitBuffer bb;
        for (const QrSegment &seg : segs)
        {
            bb.appendBits(static_cast<uint32_t>(seg.getMode().getModeBits()), 4);
            bb.appendBits(static_cast<uint32_t>(seg.getNumChars()), seg.getMode().numCharCountBits(version));
            bb.insert(bb.end(), seg.getData().begin(), seg.getData().end());
        }
        assert(bb.size() == static_cast<unsigned int>(dataUsedBits));

        // Add terminator and pad up to a byte if applicable
        size_t dataCapacityBits = static_cast<size_t>(getNumDataCodewords(version, ecl)) * 8;
        assert(bb.size() <= dataCapacityBits);
        bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
        bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
        assert(bb.size() % 8 == 0);

        // Pad with alternating bytes until data capacity is reached
        for (uint8_t padByte = 0xEC; bb.size() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
            bb.appendBits(padByte, 8);

        // Pack bits into bytes in big endian
        vector<uint8_t> dataCodewords(bb.size() / 8);
        for (size_t i = 0; i < bb.size(); i++)
            dataCodewords.at(i >> 3) |= (bb.at(i) ? 1 : 0) << (7 - (i & 7));

        // Create the QR Code object
        return QrCode(version, ecl, dataCodewords, mask);
    }

    inline QrCode::QrCode(int ver, Ecc ecl, const vector<uint8_t> &dataCodewords, int msk) : // Initialize fields and check arguments
                                                                                      version(ver),
                                                                                      errorCorrectionLevel(ecl)
    {
        if (ver < MIN_VERSION || ver > MAX_VERSION)
            throw std::domain_error("Version value out of range");
        if (msk < -1 || msk > 7)
            throw std::domain_error("Mask value out of range");
        size = ver * 4 + 17;
        size_t sz = static_cast<size_t>(size);
        modules = vector<vector<bool>>(sz, vector<bool>(sz)); // Initially all light
        isFunction = vector<vector<bool>>(sz, vector<bool>(sz));

        // Compute ECC, draw modules
        drawFunctionPatterns();
        const vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
        drawCodewords(allCodewords);

        // Do masking
        if (msk == -1)
        { // Automatically choose best mask
            long minPenalty = LONG_MAX;
            for (int i = 0; i < 8; i++)
            {
                applyMask(i);
                drawFormatBits(i);
                long penalty = getPenaltyScore();
                if (penalty < minPenalty)
                {
                    msk = i;
                    minPenalty = penalty;
                }
                applyMask(i); // Undoes the mask due to XOR
            }
        }
        assert(0 <= msk && msk <= 7);
        mask = msk;
        applyMask(msk);      // Apply the final choice of mask
        drawFormatBits(msk); // Overwrite old format bits

        isFunction.clear();
        isFunction.shrink_to_fit();
    }

    inline int QrCode::getVersion() const
    {
        return version;
    }

    inline int QrCode::getSize() const
    {
        return size;
    }

    inline QrCode::Ecc QrCode::getErrorCorrectionLevel() const
    {
        return errorCorrectionLevel;
    }

    inline int QrCode::getMask() const
    {
        return mask;
    }

    inline bool QrCode::getModule(int x, int y) const
    {
        return 0 <= x && x < size && 0 <= y && y < size && module(x, y);
    }

    inline void QrCode::drawFunctionPatterns()
    {
        // Draw horizontal and vertical timing patterns
        for (int i = 0; i < size; i++)
        {
            setFunctionModule(6, i, i % 2 == 0);
            setFunctionModule(i, 6, i % 2 == 0);
        }

        // Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
        drawFinderPattern(3, 3);
        drawFinderPattern(size - 4, 3);
        drawFinderPattern(3, size - 4);

        // Draw numerous alignment patterns
        const vector<int> alignPatPos = getAlignmentPatternPositions();
        size_t numAlign = alignPatPos.size();
        for (size_t i = 0; i < numAlign; i++)
        {
            for (size_t j = 0; j < numAlign; j++)
            {
                // Don't draw on the three finder corners
                if (!((i == 0 && j == 0) || (i == 0 && j == numAlign - 1) || (i == numAlign - 1 && j == 0)))
                    drawAlignmentPattern(alignPatPos.at(i), alignPatPos.at(j));
            }
        }

        // Draw configuration data
        drawFormatBits(0); // Dummy mask value; overwritten later in the constructor
        drawVersion();
    }

    inline void QrCode::drawFormatBits(int msk)
    {
        // Calculate error correction code and pack bits
        int data = getFormatBits(errorCorrectionLevel) << 3 | msk; // errCorrLvl is uint2, msk is uint3
        int rem = data;
        for (int i = 0; i < 10; i++)
            rem = (rem << 1) ^ ((rem >> 9) * 0x537);
        int bits = (data << 10 | rem) ^ 0x5412; // uint15
        assert(bits >> 15 == 0);

        // Draw first copy
        for (int i = 0; i <= 5; i++)
            setFunctionModule(8, i, getBit(bits, i));
        setFunctionModule(8, 7, getBit(bits, 6));
        setFunctionModule(8, 8, getBit(bits, 7));
        setFunctionModule(7, 8, getBit(bits, 8));
        for (int i = 9; i < 15; i++)
            setFunctionModule(14 - i, 8, getBit(bits, i));

        // Draw second copy
        for (int i = 0; i < 8; i++)
            setFunctionModule(size - 1 - i, 8, getBit(bits, i));
        for (int i = 8; i < 15; i++)
            setFunctionModule(8, size - 15 + i, getBit(bits, i));
        setFunctionModule(8, size - 8, true); // Always dark
    }

    inline void QrCode::drawVersion()
    {
        if (version < 7)
            return;

        // Calculate error correction code and pack bits
        int rem = version; // version is uint6, in the range [7, 40]
        for (int i = 0; i < 12; i++)
            rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
        long bits = static_cast<long>(version) << 12 | rem; // uint18
        assert(bits >> 18 == 0);

        // Draw two copies
        for (int i = 0; i < 18; i++)
        {
            bool bit = getBit(bits, i);
            int a = size - 11 + i % 3;
            int b = i / 3;
            setFunctionModule(a, b, bit);
            setFunctionModule(b, a, bit);
        }
    }

    inline void QrCode::drawFinderPattern(int x, int y)
    {
        for (int dy = -4; dy <= 4; dy++)
        {
            for (int dx = -4; dx <= 4; dx++)
            {
                int dist = std::max(std::abs(dx), std::abs(dy)); // Chebyshev/infinity norm
                int xx = x + dx, yy = y + dy;
                if (0 <= xx && xx < size && 0 <= yy && yy < size)
                    setFunctionModule(xx, yy, dist != 2 && dist != 4);
            }
        }
    }

    inline void QrCode::drawAlignmentPattern(int x, int y)
    {
        for (int dy = -2; dy <= 2; dy++)
        {
            for (int dx = -2; dx <= 2; dx++)
                setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
        }
    }

    inline void QrCode::setFunctionModule(int x, int y, bool isDark)
    {
        size_t ux = static_cast<size_t>(x);
        size_t uy = static_cast<size_t>(y);
        modules.at(uy).at(ux) = isDark;
        isFunction.at(uy).at(ux) = true;
    }

    inline bool QrCode::module(int x, int y) const
    {
        return modules.at(static_cast<size_t>(y)).at(static_cast<size_t>(x));
    }

    inline vector<uint8_t> QrCode::addEccAndInterleave(const vector<uint8_t> &data) const
    {
        if (data.size() != static_cast<unsigned int>(getNumDataCodewords(version, errorCorrectionLevel)))
            throw std::invalid_argument("Invalid argument");

        // Calculate parameter numbers
        int numBlocks = NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(errorCorrectionLevel)][version];
        int blockEccLen = ECC_CODEWORDS_PER_BLOCK[static_cast<int>(errorCorrectionLevel)][version];
        int rawCodewords = getNumRawDataModules(version) / 8;
        int numShortBlocks = numBlocks - rawCodewords % numBlocks;
        int shortBlockLen = rawCodewords / numBlocks;

        // Split data into blocks and append ECC to each block
        vector<vector<uint8_t>> blocks;
        const vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
        for (int i = 0, k = 0; i < numBlocks; i++)
        {
            vector<uint8_t> dat(data.cbegin() + k, data.cbegin() + (k + shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1)));
            k += static_cast<int>(dat.size());
            const vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
            if (i < numShortBlocks)
                dat.push_back(0);
            dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
            blocks.push_back(std::move(dat));
        }

        // Interleave (not concatenate) the bytes from every block into a single sequence
        vector<uint8_t> result;
        for (size_t i = 0; i < blocks.at(0).size(); i++)
        {
            for (size_t j = 0; j < blocks.size(); j++)
            {
                // Skip the padding byte in short blocks
                if (i != static_cast<unsigned int>(shortBlockLen - blockEccLen) || j >= static_cast<unsigned int>(numShortBlocks))
                    result.push_back(blocks.at(j).at(i));
            }
        }
        assert(result.size() == static_cast<unsigned int>(rawCodewords));
        return result;
    }

    inline void QrCode::drawCodewords(const vector<uint8_t> &data)
    {
        if (data.size() != static_cast<unsigned int>(getNumRawDataModules(version) / 8))
            throw std::invalid_argument("Invalid argument");

        size_t i = 0; // Bit index into the data
        // Do the funny zigzag scan
        for (int right = size - 1; right >= 1; right -= 2)
        { // Index of right column in each column pair
            if (right == 6)
                right = 5;
            for (int vert = 0; vert < size; vert++)
            { // Vertical counter
                for (int j = 0; j < 2; j++)
                {
                    size_t x = static_cast<size_t>(right - j); // Actual x coordinate
                    bool upward = ((right + 1) & 2) == 0;
                    size_t y = static_cast<size_t>(upward ? size - 1 - vert : vert); // Actual y coordinate
                    if (!isFunction.at(y).at(x) && i < data.size() * 8)
                    {
                        modules.at(y).at(x) = getBit(data.at(i >> 3), 7 - static_cast<int>(i & 7));
                        i++;
                    }
                    // If this QR Code has any remainder bits (0 to 7), they were assigned as
                    // 0/false/light by the constructor and are left unchanged by this method
                }
            }
        }
        assert(i == data.size() * 8);
    }

    inline void QrCode::applyMask(int msk)
    {
        if (msk < 0 || msk > 7)
            throw std::domain_error("Mask value out of range");
        size_t sz = static_cast<size_t>(size);
        for (size_t y = 0; y < sz; y++)
        {
            for (size_t x = 0; x < sz; x++)
            {
                bool invert;
                switch (msk)
                {
                case 0:
                    invert = (x + y) % 2 == 0;
                    break;
                case 1:
                    invert = y % 2 == 0;
                    break;
                case 2:
                    invert = x % 3 == 0;
                    break;
                case 3:
                    invert = (x + y) % 3 == 0;
                    break;
                case 4:
                    invert = (x / 3 + y / 2) % 2 == 0;
                    break;
                case 5:
                    invert = x * y % 2 + x * y % 3 == 0;
                    break;
                case 6:
                    invert = (x * y % 2 + x * y % 3) % 2 == 0;
                    break;
                case 7:
                    invert = ((x + y) % 2 + x * y % 3) % 2 == 0;
                    break;
                default:
                    throw std::logic_error("Unreachable");
                }
                modules.at(y).at(x) = modules.at(y).at(x) ^ (invert & !isFunction.at(y).at(x));
            }
        }
    }

    inline long QrCode::getPenaltyScore() const
    {
        long result = 0;

        // Adjacent modules in row having same color, and finder-like patterns
        for (int y = 0; y < size; y++)
        {
            bool runColor = false;
            int runX = 0;
            std::array<int, 7> runHistory = {};
            for (int x = 0; x < size; x++)
            {
                if (module(x, y) == runColor)
                {
                    runX++;
                    if (runX == 5)
                        result += PENALTY_N1;
                    else if (runX > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runX, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
                    runColor = module(x, y);
                    runX = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runX, runHistory) * PENALTY_N3;
        }
        // Adjacent modules in column having same color, and finder-like patterns
        for (int x = 0; x < size; x++)
        {
            bool runColor = false;
            int runY = 0;
            std::array<int, 7> runHistory = {};
            for (int y = 0; y < size; y++)
            {
                if (module(x, y) == runColor)
                {
                    runY++;
                    if (runY == 5)
                        result += PENALTY_N1;
                    else if (runY > 5)
                        result++;
                }
                else
                {
                    finderPenaltyAddHistory(runY, runHistory);
                    if (!runColor)
                        result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
                    runColor = module(x, y);
                    runY = 1;
                }
            }
            result += finderPenaltyTerminateAndCount(runColor, runY, runHistory) * PENALTY_N3;
        }

        // 2*2 blocks of modules having same color
        for (int y = 0; y < size - 1; y++)
        {
            for (int x = 0; x < size - 1; x++)
            {
                bool color = module(x, y);
                if (color == module(x + 1, y) &&
                    color == module(x, y + 1) &&
                    color == module(x + 1, y + 1))
                    result += PENALTY_N2;
            }
        }

        // Balance of dark and light modules
        int dark = 0;
        for (const vector<bool> &row : modules)
        {
            for (bool color : row)
            {
                if (color)
                    dark++;
            }
        }
        int total = size * size; // Note that size is odd, so dark/total != 1/2
        // Compute the smallest integer k >= 0 such that (45-5k)% <= dark/total <= (55+5k)%
        int k = static_cast<int>((std::abs(dark * 20L - total * 10L) + total - 1) / total) - 1;
        assert(0 <= k && k <= 9);
        result += k * PENALTY_N4;
        assert(0 <= result && result <= 2568888L); // Non-tight upper bound based on default values of PENALTY_N1, ..., N4
        return result;
    }

    inline vector<int> QrCode::getAlignmentPatternPositions() const
    {
        if (version == 1)
            return vector<int>();
        else
        {
            int numAlign = version / 7 + 2;
            int step = (version * 8 + numAlign * 3 + 5) / (numAlign * 4 - 4) * 2;
            vector<int> result;
            for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
                result.insert(result.begin(), pos);
            result.insert(result.begin(), 6);
            return result;
        }
    }

    inline int QrCode::getNumRawDataModules(int ver)
    {
        if (ver < MIN_VERSION || ver > MAX_VERSION)
            throw std::domain_error("Version number out of range");
        int result = (16 * ver + 128) * ver + 64;
        if (ver >= 2)
        {
            int numAlign = ver / 7 + 2;
            result -= (25 * numAlign - 10) * numAlign - 55;
            if (ver >= 7)
                result -= 36;
        }
        assert(208 <= result && result <= 29648);
        return result;
    }

    inline int QrCode::getNumDataCodewords(int ver, Ecc ecl)
    {
        return getNumRawDataModules(ver) / 8 - ECC_CODEWORDS_PER_BLOCK[static_cast<int>(ecl)][ver] * NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(ecl)][ver];
    }

    inline vector<uint8_t> QrCode::reedSolomonComputeDivisor(int degree)
    {
        if (degree < 1 || degree > 255)
            throw std::domain_error("Degree out of range");
        // Polynomial coefficients are stored from highest to lowest power, excluding the leading term which is always 1.
        // For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
        vector<uint8_t> result(static_cast<size_t>(degree));
        result.at(result.size() - 1) = 1; // Start off with the monomial x^0

        // Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
        // and drop the highest monomial term which is always 1x^degree.
        // Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
        uint8_t root = 1;
        for (int i = 0; i < degree; i++)
        {
            // Multiply the current product by (x - r^i)
            for (size_t j = 0; j < result.size(); j++)
            {
                result.at(j) = reedSolomonMultiply(result.at(j), root);
                if (j + 1 < result.size())
                    result.at(j) ^= result.at(j + 1);
            }
            root = reedSolomonMultiply(root, 0x02);
        }
        return result;
    }

    inline vector<uint8_t> QrCode::reedSolomonComputeRemainder(const vector<uint8_t> &data, const vector<uint8_t> &divisor)
    {
        vector<uint8_t> result(divisor.size());
        for (uint8_t b : data)
        { // Polynomial division
            uint8_t factor = b ^ result.at(0);
            result.erase(result.begin());
            result.push_back(0);
            for (size_t i = 0; i < result.size(); i++)
                result.at(i) ^= reedSolomonMultiply(divisor.at(i), factor);
        }
        return result;
    }

    inline uint8_t QrCode::reedSolomonMultiply(uint8_t x, uint8_t y)
    {
        // Russian peasant multiplication
        int z = 0;
        for (int i = 7; i >= 0; i--)
        {
            z = (z << 1) ^ ((z >> 7) * 0x11D);
            z ^= ((y >> i) & 1) * x;
        }
        assert(z >> 8 == 0);
        return static_cast<uint8_t>(z);
    }

    inline int QrCode::finderPenaltyCountPatterns(const std::array<int, 7> &runHistory) const
    {
        int n = runHistory.at(1);
        assert(n <= size * 3);
        bool core = n > 0 && runHistory.at(2) == n && runHistory.at(3) == n * 3 && runHistory.at(4) == n && runHistory.at(5) == n;
        return (core && runHistory.at(0) >= n * 4 && runHistory.at(6) >= n ? 1 : 0) + (core && runHistory.at(6) >= n * 4 && runHistory.at(0) >= n ? 1 : 0);
    }

    inline int QrCode::finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int, 7> &runHistory) const
    {
        if (currentRunColor)
        { // Terminate dark run
            finderPenaltyAddHistory(currentRunLength, runHistory);
            currentRunLength = 0;
        }
        currentRunLength += size; // Add light border to final run
        finderPenaltyAddHistory(currentRunLength, runHistory);
        return finderPenaltyCountPatterns(runHistory);
    }

    inline void QrCode::finderPenaltyAddHistory(int currentRunLength, std::array<int, 7> &runHistory) const
    {
        if (runHistory.at(0) == 0)
            currentRunLength += size; // Add light border to initial run
        std::copy_backward(runHistory.cbegin(), runHistory.cend() - 1, runHistory.end());
        runHistory.at(0) = currentRunLength;
    }

    inline bool QrCode::getBit(long x, int i)
    {
        return ((x >> i) & 1) != 0;
    }

    /*---- Tables of constants ----*/

    inline const int QrCode::PENALTY_N1 = 3;
    inline const int QrCode::PENALTY_N2 = 3;
    inline const int QrCode::PENALTY_N3 = 40;
    inline const int QrCode::PENALTY_N4 = 10;

    const int8_t QrCode::ECC_CODEWORDS_PER_BLOCK[4][41] = {
        // Version: (note that index 0 is for padding, and is set to an illegal value)
        // 0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
        {-1, 7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Low
        {-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28}, // Medium
        {-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // Quartile
        {-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30}, // High
    };

    const int8_t QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
        // Version: (note that index 0 is for padding, and is set to an illegal value)
        // 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
        {-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4, 4, 4, 4, 4, 6, 6, 6, 6, 7, 8, 8, 9, 9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},              // Low
        {-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5, 5, 8, 9, 9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},     // Medium
        {-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8, 8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
        {-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81}, // High
    };

    inline data_too_long::data_too_long(const std::string &msg) : std::length_error(msg) {}

    /*---- Class BitBuffer ----*/

    inline BitBuffer::BitBuffer()
        : std::vector<bool>() {}

    inline void BitBuffer::appendBits(std::uint32_t val, int len)
    {
        if (len < 0 || len > 31 || val >> len != 0)
            throw std::domain_error("Value out of range");
        for (int i = len - 1; i >= 0; i--) // Append bit by bit
            this->push_back(((val >> i) & 1) != 0);
    }

}
