from pathlib import Path
import struct, sys
def unpack(data):
    length = int.from_bytes(data[2:5], 'big')
    i, out = 5, bytearray()
    while i < len(data):
        c = data[i]; i += 1
        if c >= 252:
            n = c & 3
            out.extend(data[i:i+n])
            break
        if c >= 224:
            n = ((c & 31)+1)*4
            count = offset = 0
        elif c >= 192:
            a,b,d = data[i:i+3]; i += 3
            n, count, offset = c&3, ((c&12)<<6)+d+5, ((c&16)<<12)+(a<<8)+b+1
        elif c >= 128:
            a,b = data[i:i+2]; i += 2
            n, count, offset = a>>6, (c&63)+4, ((a&63)<<8)+b+1
        else:
            a = data[i]; i += 1
            n, count, offset = c&3, ((c&28)>>2)+3, ((c&96)<<3)+a+1
        out.extend(data[i:i+n]); i += n
        for _ in range(count):
            out.append(out[-offset])
    assert len(out)==length, (len(out),length)
    return out
def resources(path, types=None):
    with path.open('rb') as f:
        header=f.read(96)
        assert header[:4]==b'DBPF'
        count=struct.unpack_from('<I',header,36)[0]
        f.seek(struct.unpack_from('<I',header,64)[0])
        index=f.read(struct.unpack_from('<I',header,44)[0])
        flags,=struct.unpack_from('<I',index)
        i=4
        shared={}
        for j in range(3):
            if flags & (1<<j):
                shared[j],=struct.unpack_from('<I',index,i); i+=4
        for _ in range(count):
            fields=[]
            for j in range(8):
                if j in shared:
                    fields.append(shared[j])
                else:
                    fields.append(struct.unpack_from('<I',index,i)[0]); i+=4
            t,g,h,l,pos,size,raw,comp=fields
            if types is not None and t not in types: continue
            f.seek(pos)
            blob=f.read(size&0x7fffffff)
            if comp&0xffff==0xffff:
                blob=unpack(blob)
            assert len(blob)==raw
            yield t,g,(h<<32)|l,blob
if __name__=='__main__':
    for arg in sys.argv[1:]:
        path=Path(arg)
        for t,g,i,blob in resources(path):
            key=f'{t:08X}-{g:08X}-{i:016X}'
            print(path.name,key,len(blob),bytes(blob[:40]))
            Path('work',path.parent.name+'_'+path.stem+'_'+key).write_bytes(blob)
