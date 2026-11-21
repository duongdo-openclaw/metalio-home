#!/usr/bin/env python3
"""Builds the small sample dictionaries used by the simulator and the tests.

  en-vi-mau.ifo/.idx/.dict.dz  StarDict, sametypesequence=m, dictzip-compressed (random access)
  tu-dien-viet-mau.tsv         "word<TAB>definition" list

The entries are written for this project (not copied from any published dictionary).
Usage: make_sample_dict.py OUT_DIR
"""
import os, struct, sys, zlib

EN_VI = {
 "book": "/bʊk/ (n) quyển sách; (v) đặt trước (vé, phòng)",
 "read": "/riːd/ (v) đọc; read aloud: đọc to",
 "reader": "/ˈriːdə/ (n) người đọc; máy đọc sách",
 "page": "/peɪdʒ/ (n) trang (sách, báo)",
 "word": "/wɜːd/ (n) từ; lời nói; keep one's word: giữ lời",
 "story": "/ˈstɔːri/ (n) câu chuyện; truyện",
 "fox": "/fɒks/ (n) con cáo; (nghĩa bóng) người xảo quyệt",
 "grape": "/ɡreɪp/ (n) quả nho; sour grapes: chê vì không với tới được",
 "hare": "/heə/ (n) con thỏ rừng",
 "tortoise": "/ˈtɔːtəs/ (n) con rùa (sống trên cạn)",
 "race": "/reɪs/ (n) cuộc đua; (v) chạy đua",
 "slow": "/sləʊ/ (adj) chậm; (v) slow down: chậm lại",
 "steady": "/ˈstedi/ (adj) đều đặn, vững vàng; slow and steady: chậm mà chắc",
 "win": "/wɪn/ (v) thắng, giành được (won, won)",
 "lose": "/luːz/ (v) thua; đánh mất (lost, lost)",
 "sleep": "/sliːp/ (v) ngủ (slept, slept); (n) giấc ngủ",
 "proud": "/praʊd/ (adj) tự hào; kiêu ngạo",
 "laugh": "/lɑːf/ (v) cười; laugh at: cười nhạo",
 "finish": "/ˈfɪnɪʃ/ (v) kết thúc, hoàn thành; finish line: vạch đích",
 "line": "/laɪn/ (n) dòng, đường kẻ; hàng",
 "tired": "/ˈtaɪəd/ (adj) mệt mỏi; tired of: chán",
 "hungry": "/ˈhʌŋɡri/ (adj) đói",
 "jump": "/dʒʌmp/ (v) nhảy",
 "high": "/haɪ/ (adj) cao; (adv) ở trên cao",
 "reach": "/riːtʃ/ (v) với tới; đến được",
 "sour": "/ˈsaʊə/ (adj) chua; (nghĩa bóng) khó chịu",
 "sweet": "/swiːt/ (adj) ngọt; dễ thương",
 "ripe": "/raɪp/ (adj) chín (quả)",
 "vine": "/vaɪn/ (n) cây nho; dây leo",
 "branch": "/brɑːntʃ/ (n) cành cây; chi nhánh",
 "try": "/traɪ/ (v) thử, cố gắng (tried); (n) lần thử",
 "again": "/əˈɡen/ (adv) lần nữa, lại",
 "walk": "/wɔːk/ (v) đi bộ; (n) cuộc đi dạo",
 "away": "/əˈweɪ/ (adv) đi xa, rời khỏi",
 "say": "/seɪ/ (v) nói (said, said)",
 "think": "/θɪŋk/ (v) nghĩ, cho rằng (thought, thought)",
 "know": "/nəʊ/ (v) biết (knew, known)",
 "wise": "/waɪz/ (adj) khôn ngoan, sáng suốt",
 "lesson": "/ˈlesn/ (n) bài học",
 "learn": "/lɜːn/ (v) học, học được (learned/learnt)",
 "study": "/ˈstʌdi/ (v) học, nghiên cứu (studied); (n) việc học",
 "knowledge": "/ˈnɒlɪdʒ/ (n) kiến thức, tri thức",
 "habit": "/ˈhæbɪt/ (n) thói quen",
 "daily": "/ˈdeɪli/ (adj) hằng ngày",
 "goal": "/ɡəʊl/ (n) mục tiêu; bàn thắng",
 "minute": "/ˈmɪnɪt/ (n) phút; một lát",
 "chapter": "/ˈtʃæptə/ (n) chương (sách)",
 "note": "/nəʊt/ (n) ghi chú; (v) ghi lại, lưu ý",
 "highlight": "/ˈhaɪlaɪt/ (v) đánh dấu, làm nổi bật; (n) điểm nổi bật",
 "quote": "/kwəʊt/ (n) câu trích dẫn; (v) trích dẫn",
 "dictionary": "/ˈdɪkʃənri/ (n) từ điển",
 "meaning": "/ˈmiːnɪŋ/ (n) ý nghĩa",
 "language": "/ˈlæŋɡwɪdʒ/ (n) ngôn ngữ",
 "english": "/ˈɪŋɡlɪʃ/ (n) tiếng Anh; (adj) thuộc nước Anh",
 "light": "/laɪt/ (n) ánh sáng; đèn; (adj) nhẹ",
 "night": "/naɪt/ (n) đêm, buổi tối",
 "morning": "/ˈmɔːnɪŋ/ (n) buổi sáng",
 "quiet": "/ˈkwaɪət/ (adj) yên tĩnh",
 "garden": "/ˈɡɑːdn/ (n) khu vườn",
 "forest": "/ˈfɒrɪst/ (n) khu rừng",
 "river": "/ˈrɪvə/ (n) con sông",
 "road": "/rəʊd/ (n) con đường",
 "home": "/həʊm/ (n) nhà, tổ ấm; (adv) về nhà",
 "friend": "/frend/ (n) người bạn",
 "animal": "/ˈænɪml/ (n) động vật, con vật",
 "small": "/smɔːl/ (adj) nhỏ, bé",
 "big": "/bɪɡ/ (adj) to, lớn",
 "fast": "/fɑːst/ (adj, adv) nhanh",
 "quick": "/kwɪk/ (adj) nhanh, mau lẹ",
 "patient": "/ˈpeɪʃnt/ (adj) kiên nhẫn; (n) bệnh nhân",
 "patience": "/ˈpeɪʃns/ (n) sự kiên nhẫn",
 "kind": "/kaɪnd/ (adj) tốt bụng; (n) loại",
 "brave": "/breɪv/ (adj) dũng cảm",
 "honest": "/ˈɒnɪst/ (adj) trung thực",
 "happy": "/ˈhæpi/ (adj) vui vẻ, hạnh phúc",
 "sad": "/sæd/ (adj) buồn",
 "angry": "/ˈæŋɡri/ (adj) tức giận",
 "afraid": "/əˈfreɪd/ (adj) sợ hãi",
 "believe": "/bɪˈliːv/ (v) tin, tin tưởng",
 "begin": "/bɪˈɡɪn/ (v) bắt đầu (began, begun)",
 "end": "/end/ (n) phần cuối, kết thúc; (v) kết thúc",
 "time": "/taɪm/ (n) thời gian; lần",
 "day": "/deɪ/ (n) ngày",
 "year": "/jɪə/ (n) năm",
 "world": "/wɜːld/ (n) thế giới",
 "life": "/laɪf/ (n) cuộc sống, đời sống",
 "dream": "/driːm/ (n) giấc mơ, ước mơ; (v) mơ",
 "hope": "/həʊp/ (n, v) hi vọng",
 "remember": "/rɪˈmembə/ (v) nhớ, ghi nhớ",
 "forget": "/fəˈɡet/ (v) quên (forgot, forgotten)",
 "ask": "/ɑːsk/ (v) hỏi; yêu cầu",
 "answer": "/ˈɑːnsə/ (n) câu trả lời; (v) trả lời",
 "question": "/ˈkwestʃən/ (n) câu hỏi",
 "simple": "/ˈsɪmpl/ (adj) đơn giản",
 "difficult": "/ˈdɪfɪkəlt/ (adj) khó",
 "important": "/ɪmˈpɔːtnt/ (adj) quan trọng",
 "beautiful": "/ˈbjuːtɪfl/ (adj) đẹp",
 "wonderful": "/ˈwʌndəfl/ (adj) tuyệt vời",
 "careful": "/ˈkeəfl/ (adj) cẩn thận",
 "careless": "/ˈkeələs/ (adj) cẩu thả, bất cẩn",
 "boast": "/bəʊst/ (v) khoe khoang, khoác lác",
 "challenge": "/ˈtʃælɪndʒ/ (n) thử thách; (v) thách đấu",
 "nap": "/næp/ (n) giấc ngủ ngắn; take a nap: chợp mắt",
 "wake": "/weɪk/ (v) thức dậy (woke, woken); wake up",
 "shout": "/ʃaʊt/ (v) la hét, hô to",
 "cheer": "/tʃɪə/ (v) cổ vũ, hoan hô",
 "crowd": "/kraʊd/ (n) đám đông",
 "path": "/pɑːθ/ (n) lối đi, con đường mòn",
 "hill": "/hɪl/ (n) ngọn đồi",
 "shade": "/ʃeɪd/ (n) bóng râm",
 "tree": "/triː/ (n) cái cây",
 "sun": "/sʌn/ (n) mặt trời; ánh nắng",
 "warm": "/wɔːm/ (adj) ấm áp",
 "the": "/ðə/ mạo từ xác định: cái, con, người… (đã biết)",
 "a": "/ə/ mạo từ không xác định: một",
 "and": "/ənd/ (conj) và",
 "but": "/bʌt/ (conj) nhưng",
 "never": "/ˈnevə/ (adv) không bao giờ",
 "always": "/ˈɔːlweɪz/ (adv) luôn luôn",
 "once": "/wʌns/ (adv) một lần; once upon a time: ngày xửa ngày xưa",
 "upon": "/əˈpɒn/ (prep) trên, vào lúc",
}

VI = {
 "tri thức": "Những hiểu biết có hệ thống về sự vật, hiện tượng, do học tập và trải nghiệm mà có.",
 "kiến thức": "Những điều hiểu biết có được nhờ học hỏi hoặc từng trải.",
 "phong tục": "Thói quen, nếp sống đã thành lệ, được nhiều người trong cộng đồng làm theo.",
 "tập quán": "Thói quen hình thành từ lâu trong đời sống, được mọi người làm theo.",
 "binh pháp": "Phép dùng binh; lý luận về cách tổ chức và tiến hành chiến tranh.",
 "chiến lược": "Phương châm và kế hoạch có tính toàn cục, lâu dài để đạt mục tiêu.",
 "quyền lực": "Quyền định đoạt mọi công việc và sức mạnh để bảo đảm việc thực hiện quyền ấy.",
 "nghịch lý": "Điều có vẻ trái với lẽ thường nhưng ngẫm kỹ lại có lý.",
 "thành công": "Đạt được kết quả như mong muốn.",
 "kiên nhẫn": "Bền bỉ, không nản lòng dù gặp khó khăn hay phải chờ đợi lâu.",
 "khiêm tốn": "Có ý thức và thái độ đúng mực trong việc đánh giá bản thân, không tự mãn.",
 "tự hào": "Lấy làm hãnh diện về điều tốt đẹp mà mình hoặc người thân có.",
 "thói quen": "Lối sống, cách làm lặp đi lặp lại nhiều lần thành nếp, khó thay đổi.",
 "mục tiêu": "Đích cụ thể cần đạt tới trong một hoạt động hay một khoảng thời gian.",
 "sách": "Tập giấy in hoặc viết, đóng lại thành quyển, chứa đựng tri thức hoặc câu chuyện.",
 "đọc": "Nhìn chữ và hiểu nội dung; phát ra thành tiếng theo chữ viết.",
 "học": "Thu nhận kiến thức, rèn luyện kỹ năng bằng cách nghe giảng, đọc, thực hành.",
 "ngày xưa": "Thời đã qua từ lâu.",
 "con bò": "Loài thú nhai lại, sừng ngắn, được nuôi để kéo cày, lấy thịt, sữa.",
 "marketing": "(từ mượn) Tiếp thị: các hoạt động nghiên cứu thị trường, quảng bá và bán sản phẩm.",
}

CHUNK = 58315

def dictzip(raw: bytes) -> bytes:
    chunks = []
    for i in range(0, len(raw), CHUNK):
        c = zlib.compressobj(9, zlib.DEFLATED, -15)
        data = c.compress(raw[i:i + CHUNK]) + c.flush(zlib.Z_FULL_FLUSH)
        chunks.append(data)
    last = zlib.compressobj(9, zlib.DEFLATED, -15).flush(zlib.Z_FINISH)  # empty final block
    sizes = [len(c) for c in chunks]
    ra = struct.pack('<HHH', 1, CHUNK, len(chunks)) + b''.join(struct.pack('<H', s) for s in sizes)
    extra = b'RA' + struct.pack('<H', len(ra)) + ra
    head = b'\x1f\x8b\x08\x04' + b'\x00\x00\x00\x00' + b'\x02\x03' + struct.pack('<H', len(extra)) + extra
    tail = struct.pack('<II', zlib.crc32(raw) & 0xffffffff, len(raw) & 0xffffffff)
    return head + b''.join(chunks) + last + tail

def casekey(w):
    return (w.lower(), w)

def main(out):
    os.makedirs(out, exist_ok=True)
    words = sorted(EN_VI, key=casekey)
    data, idx = b'', b''
    for w in words:
        d = EN_VI[w].encode()
        idx += w.encode() + b'\0' + struct.pack('>II', len(data), len(d))
        data += d
    base = os.path.join(out, 'en-vi-mau')
    open(base + '.idx', 'wb').write(idx)
    open(base + '.dict.dz', 'wb').write(dictzip(data))
    open(base + '.ifo', 'w').write(
        "StarDict's dict ifo file\nversion=2.4.2\nbookname=Anh - Việt (mẫu)\n"
        f"wordcount={len(words)}\nidxfilesize={len(idx)}\nsametypesequence=m\n"
        "description=Từ điển mẫu nhỏ để thử tính năng tra từ. Chép từ điển StarDict đầy đủ vào thư mục dict/.\n")
    with open(os.path.join(out, 'tu-dien-viet-mau.tsv'), 'w') as f:
        f.write('# Từ điển tiếng Việt mẫu: từ<TAB>nghĩa\n')
        for k in sorted(VI):
            f.write(f'{k}\t{VI[k]}\n')
    print(len(words), 'EN-VI words,', len(VI), 'VI words ->', out)

if __name__ == '__main__':
    main(sys.argv[1])
