import struct
import hashlib
import sys

def get_v2_hash(apk_path):
    with open(apk_path, 'rb') as f:
        # Read the End of Central Directory Record
        f.seek(-22, 2)
        eocd = f.read(22)
        if eocd[:4] != b'PK\x05\x06':
            # Try searching backwards if there's a comment
            f.seek(-1024, 2)
            data = f.read()
            idx = data.rfind(b'PK\x05\x06')
            if idx == -1:
                return None
            eocd = data[idx:idx+22]
            f.seek(-(len(data) - idx), 2)
        
        # Get offset of start of central directory
        cd_offset = struct.unpack('<I', eocd[16:20])[0]
        
        # Read APK Signing Block
        f.seek(cd_offset - 24)
        size_of_block, magic = struct.unpack('<Q16s', f.read(24))
        if magic != b'APK Sig Block 42':
            print("No APK Sig Block 42 found")
            return None
            
        f.seek(cd_offset - (size_of_block + 8))
        size_of_block_2 = struct.unpack('<Q', f.read(8))[0]
        if size_of_block != size_of_block_2:
            return None
            
        # Parse the block
        bytes_read = 0
        while bytes_read < size_of_block - 24:
            seq_len = struct.unpack('<Q', f.read(8))[0]
            id = struct.unpack('<I', f.read(4))[0]
            if id == 0x7109871a: # APK Signature Scheme v2
                # parse signer sequence
                signer_seq_len = struct.unpack('<I', f.read(4))[0]
                signer_len = struct.unpack('<I', f.read(4))[0]
                signed_data_len = struct.unpack('<I', f.read(4))[0]
                
                # skip signed data
                f.seek(signed_data_len, 1)
                
                # read digests
                digests_seq_len = struct.unpack('<I', f.read(4))[0]
                f.seek(digests_seq_len, 1)
                
                # read certificates
                certs_seq_len = struct.unpack('<I', f.read(4))[0]
                cert_len = struct.unpack('<I', f.read(4))[0]
                cert_data = f.read(cert_len)
                
                sha256 = hashlib.sha256(cert_data).hexdigest()
                size = hex(cert_len)
                print(f"SIZE={size}")
                print(f"HASH={sha256}")
                return
                
            else:
                f.seek(seq_len - 4, 1)
            bytes_read += 8 + seq_len

if __name__ == '__main__':
    get_v2_hash(sys.argv[1])
