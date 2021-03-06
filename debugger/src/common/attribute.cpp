/**
 * @file
 * @copyright  Copyright 2016 GNSS Sensor Ltd. All right reserved.
 * @author     Sergey Khabarov - sergeykhbr@gmail.com
 * @brief      Core attribute methods implementation.
 */

#include "api_core.h"
#include "autobuffer.h"
#include "iservice.h"
#include "api_utils.h"
#include <cstdlib>

namespace debugger {

static const int64_t MIN_ALLOC_BYTES = 1 << 12;
static AttributeType NilAttribute;
static AutoBuffer strBuffer;

char *attribute_to_string(const AttributeType *attr);
const char *string_to_attribute(const char *cfg, AttributeType *out);

void AttributeType::attr_free() {
    if (size()) {
        if (is_string()) {
            RISCV_free(u_.string);
        } else if (is_data() && size() > 8) {
            RISCV_free(u_.data);
        } else if (is_list()) {
            for (unsigned i = 0; i < size(); i++) {
                u_.list[i].attr_free();
            }
            RISCV_free(u_.list);
        } else if (is_dict()) {
            for (unsigned i = 0; i < size(); i++) {
                u_.dict[i].key_.attr_free();
                u_.dict[i].value_.attr_free();
            }
            RISCV_free(u_.dict);
        }
    }
    kind_ = Attr_Invalid;
    size_ = 0;
    u_.integer = 0;
}

void AttributeType::clone(const AttributeType *v) {
    attr_free();

    if (v->is_string()) {
        this->make_string(v->to_string());
    } else if (v->is_data()) {
        this->make_data(v->size(), v->data());
    } else if (v->is_list()) {
        make_list(v->size());
        for (unsigned i = 0; i < v->size(); i++ ) {
            u_.list[i].clone(v->list(i));
        }
    } else if (v->is_dict()) {
        make_dict();
        realloc_dict(v->size());
        for (unsigned i = 0; i < v->size(); i++ ) {
            u_.dict[i].key_.make_string(v->dict_key(i)->to_string());
            u_.dict[i].value_.clone(v->dict_value(i));
        }
    } else {
        this->kind_ = v->kind_;
        this->u_ = v->u_;
        this->size_ = v->size_;
    }
}

bool AttributeType::is_equal(const char *v) {
    if (!is_string()) {
        return false;
    }
    return !strcmp(to_string(), v);
}


AttributeType &AttributeType::operator=(const AttributeType& other) {
    if (&other != this) {
        clone(&other);
    }
    return *this;
}


const AttributeType &AttributeType::operator[](unsigned idx) const {
    if (is_list()) {
        return u_.list[idx];
    } else if (is_dict()) {
        return u_.dict[idx].value_;
    } else {
        RISCV_printf(NULL, LOG_ERROR, "%s", "Non-indexed attribute type");
    }
    return NilAttribute;
}

AttributeType &AttributeType::operator[](unsigned idx) {
    if (is_list()) {
        return u_.list[idx];
    } else if (is_dict()) {
        return u_.dict[idx].value_;
    } else {
        RISCV_printf(NULL, LOG_ERROR, "%s", "Non-indexed attribute type");
    }
    return NilAttribute;
}

const AttributeType &AttributeType::operator[](const char *key) const {
    for (unsigned i = 0; i < size(); i++) {
        if (strcmp(key, u_.dict[i].key_.to_string()) == 0) {
            return u_.dict[i].value_;
        }
    }
    AttributeType *pthis = const_cast<AttributeType*>(this);
    pthis->realloc_dict(size()+1);
    pthis->u_.dict[size()-1].key_.make_string(key);
    pthis->u_.dict[size()-1].value_.make_nil();
    return u_.dict[size()-1].value_;
}

AttributeType &AttributeType::operator[](const char *key) {
    for (unsigned i = 0; i < size(); i++) {
        if (strcmp(key, u_.dict[i].key_.to_string()) == 0) {
            return u_.dict[i].value_;
        }
    }
    realloc_dict(size()+1);
    u_.dict[size()-1].key_.make_string(key);
    u_.dict[size()-1].value_.make_nil();
    return u_.dict[size()-1].value_;
}

const uint8_t &AttributeType::operator()(unsigned idx) const {
    if (idx > size()) {
        RISCV_printf(NULL, LOG_ERROR, "Data index '%d' out of range.", idx);
        return u_.data[0];
    }
    if (size_ > 8) {
        return u_.data[idx];
    }
    return u_.data_bytes[idx];
}

void AttributeType::make_string(const char *value) {
    attr_free();
    if (value) {
        kind_ = Attr_String;
        size_ = (unsigned)strlen(value);
        u_.string = static_cast<char *>(RISCV_malloc(size_ + 1));
        memcpy(u_.string, value, size_ + 1);
    } else {
        kind_ = Attr_Nil;
    }
}

void AttributeType::make_data(unsigned size) {
    attr_free();
    kind_ = Attr_Data;
    size_ = size;
    if (size > 8) {
        u_.data = static_cast<uint8_t *>(RISCV_malloc(size_));
    }
}

void AttributeType::make_data(unsigned size, const void *data) {
    attr_free();
    kind_ = Attr_Data;
    size_ = size;
    if (size > 8) {
        u_.data = static_cast<uint8_t *>(RISCV_malloc(size_));
        memcpy(u_.data, data, size);
    } else {
        memcpy(u_.data_bytes, data, size);
    }
}

void AttributeType::make_list(unsigned size) {
    attr_free();
    kind_ = Attr_List;
    if (size) {
        realloc_list(size);
    }
}

void AttributeType::realloc_list(unsigned size) {
    size_t req_sz = (size * sizeof(AttributeType) + MIN_ALLOC_BYTES - 1) 
                   / MIN_ALLOC_BYTES;
    size_t cur_sz = (size_ * sizeof(AttributeType) + MIN_ALLOC_BYTES - 1) 
                  / MIN_ALLOC_BYTES;
    if (req_sz > cur_sz ) {
        AttributeType * t1 = static_cast<AttributeType *>(
                RISCV_malloc(MIN_ALLOC_BYTES * req_sz));
        memcpy(t1, u_.list, size_ * sizeof(AttributeType));
        memset(&t1[size_], 0, 
                (MIN_ALLOC_BYTES * req_sz) - size_ * sizeof(AttributeType));
        if (size_) {
            RISCV_free(u_.list);
        }
        u_.list = t1;
    }
    size_ = size;
}

void AttributeType::insert_to_list(unsigned idx, const AttributeType *item) {
    if (idx > size_) {
        RISCV_printf(NULL, LOG_ERROR, "%s", "Insert index out of bound");
        return;
    }
    size_t new_sz = ((size_ + 1) * sizeof(AttributeType) + MIN_ALLOC_BYTES - 1)
                  / MIN_ALLOC_BYTES;
    AttributeType * t1 = static_cast<AttributeType *>(
                RISCV_malloc(MIN_ALLOC_BYTES * new_sz));
    memset(t1 + idx, 0, sizeof(AttributeType));  // Fix bug request #4

    memcpy(t1, u_.list, idx * sizeof(AttributeType));
    t1[idx].clone(item);
    memcpy(&t1[idx + 1], &u_.list[idx], (size_ - idx) * sizeof(AttributeType));
    memset(&t1[size_ + 1], 0, 
          (MIN_ALLOC_BYTES * new_sz) - (size_ + 1) * sizeof(AttributeType));
    if (size_) {
        RISCV_free(u_.list);
    }
    u_.list = t1;
    size_++;
}

void AttributeType::remove_from_list(unsigned idx) {
    if (idx >= size_) {
        RISCV_printf(NULL, LOG_ERROR, "%s", "Remove index out of range");
        return;
    }
    (*this)[idx].attr_free();
    if (idx == (size() - 1)) {
        size_ -= 1;
    } else if (idx < size ()) {
        swap_list_item(idx, size() - 1);
        size_ -= 1;
    }
}

void AttributeType::trim_list(unsigned start, unsigned end) {
    for (unsigned i = start; i < (size_ - end); i++) {
        u_.list[start + i].attr_free();
        u_.list[start + i] = u_.list[end + i];
    }
    size_ -= (end - start);
}

void AttributeType::swap_list_item(unsigned n, unsigned m) {
    if (n == m) {
        return;
    }
    unsigned tsize = u_.list[n].size_;
    KindType tkind = u_.list[n].kind_;
    int64_t tinteger = u_.list[n].u_.integer;
    u_.list[n].size_ = u_.list[m].size_;
    u_.list[n].kind_ = u_.list[m].kind_;
    u_.list[n].u_.integer = u_.list[m].u_.integer;
    u_.list[m].size_ = tsize;
    u_.list[m].kind_ = tkind;
    u_.list[m].u_.integer = tinteger;
}


int partition(AttributeType *A, int lo, int hi, int lst_idx) {
    AttributeType *pivot = &(*A)[hi];
    bool do_swap;
    int i = lo - 1;
    for (int j = lo; j < hi; j++) {
        AttributeType &item = (*A)[j];
        do_swap = false;
        if (item.is_string()) {
            if (strcmp(item.to_string(), pivot->to_string()) <= 0) {
                do_swap = true;
            }
        } else if (item.is_int64()) {
            if (item.to_int64() <= pivot->to_int64()) {
                do_swap = true;
            }
        } else if (item.is_uint64()) {
            if (item.to_uint64() <= pivot->to_uint64()) {
                do_swap = true;
            }
        } else if (item.is_list()) {
            AttributeType &t1 = item[lst_idx];
            if (t1.is_string() &&
                strcmp(t1.to_string(), (*pivot)[lst_idx].to_string()) <= 0) {
                do_swap = true;
            } else if (t1.is_int64() &&
                t1.to_int64() <= (*pivot)[lst_idx].to_int64()) {
                do_swap = true;
            } else if (t1.is_uint64() &&
                t1.to_uint64() <= (*pivot)[lst_idx].to_uint64()) {
                do_swap = true;
            }
        } else {
            RISCV_printf(NULL, LOG_ERROR, "%s", 
                        "Not supported attribute type for sorting");
            return i + 1;
        }

        if (do_swap) {
            i = i + 1;
            A->swap_list_item(i, j);
        }
    }
    A->swap_list_item(i + 1, hi);
    return i + 1;
}

void quicksort(AttributeType *A, int lo, int hi, int lst_idx) {
    if (lo >= hi) {
        return;
    }
    int p = partition(A, lo, hi, lst_idx);
    quicksort(A, lo, p - 1, lst_idx);
    quicksort(A, p + 1, hi, lst_idx);
}

void AttributeType::sort(int idx) {
    if (!is_list()) {
        RISCV_printf(NULL, LOG_ERROR, "%s", 
                    "Sort algorithm can applied only to list attribute");
    }
    quicksort(this, 0, static_cast<int>(size()) - 1, idx);
}

bool AttributeType::has_key(const char *key) const {
    for (unsigned i = 0; i < size(); i++) {
        if (strcmp(u_.dict[i].key_.to_string(), key) == 0
            && !u_.dict[i].value_.is_nil()) {
            return true;
        }
    }
    return false;
}

const AttributeType *AttributeType::dict_key(unsigned idx) const {
    return &u_.dict[idx].key_;
}
AttributeType *AttributeType::dict_key(unsigned idx) {
    return &u_.dict[idx].key_;
}

const AttributeType *AttributeType::dict_value(unsigned idx) const {
    return &u_.dict[idx].value_;
}
AttributeType *AttributeType::dict_value(unsigned idx) {
    return &u_.dict[idx].value_;
}

void AttributeType::make_dict() {
    attr_free();
    kind_ = Attr_Dict;
    size_ = 0;
    u_.dict = NULL;
}

void AttributeType::realloc_dict(unsigned size) {
    size_t req_sz = (size * sizeof(AttributePairType) + MIN_ALLOC_BYTES - 1)
                  / MIN_ALLOC_BYTES;
    size_t cur_sz = (size_ * sizeof(AttributePairType) + MIN_ALLOC_BYTES - 1)
                  / MIN_ALLOC_BYTES;
    if (req_sz > cur_sz ) {
        AttributePairType * t1 = static_cast<AttributePairType *>(
                RISCV_malloc(MIN_ALLOC_BYTES * req_sz));
        memcpy(t1, u_.dict, size_ * sizeof(AttributePairType));
        memset(&t1[size_], 0, 
                (MIN_ALLOC_BYTES * req_sz) - size_ * sizeof(AttributePairType));
        if (size_) {
            RISCV_free(u_.dict);
        }
        u_.dict = t1;
    }
    size_ = size;
}

char *AttributeType::to_config() {
    strBuffer.clear();
    attribute_to_string(this);
    return strBuffer.getBuffer();
}

void AttributeType::from_config(const char *str) {
    string_to_attribute(str, this);
}

char *attribute_to_string(const AttributeType *attr) {
    IService *iserv;
    AutoBuffer *buf = &strBuffer;
    if (attr->is_nil()) {
        buf->write_string("None");
    } else if (attr->is_int64() || attr->is_uint64()) {
        buf->write_uint64(attr->to_uint64());
    } else if (attr->is_string()) {
        buf->write_string('\'');
        buf->write_string(attr->to_string());
        buf->write_string('\'');
    } else if (attr->is_bool()) {
        if (attr->to_bool()) {
            buf->write_string("true");
        } else {
            buf->write_string("false");
        }
    } else if (attr->is_list()) {
        AttributeType list_item;
        unsigned list_sz = attr->size();
        buf->write_string('[');
        for (unsigned i = 0; i < list_sz; i++) {
            list_item = (*attr)[i];
            attribute_to_string(&list_item);
            if (i < (list_sz - 1)) {
                buf->write_string(',');
            }
        }
        buf->write_string(']');
    } else if (attr->is_dict()) {
        AttributeType dict_item;
        unsigned dict_sz = attr->size();;
        buf->write_string('{');

        for (unsigned i = 0; i < dict_sz; i++) {
            buf->write_string('\'');
            buf->write_string(attr->u_.dict[i].key_.to_string());
            buf->write_string('\'');
            buf->write_string(':');
            const AttributeType &dict_value = (*attr)[i];
            attribute_to_string(&dict_value);
            if (i < (dict_sz - 1)) {
                buf->write_string(',');
            }
        }
        buf->write_string('}');
    } else if (attr->is_data()) {
        buf->write_string('(');
        if (attr->size() > 0) {
            for (unsigned n = 0; n < attr->size()-1;  n++) {
                buf->write_byte((*attr)(n));
                buf->write_string(',');
            }
            buf->write_byte((*attr)(attr->size()-1));
        }
        buf->write_string(')');
    } else if (attr->is_iface()) {
        IFace *iface = attr->to_iface();
        if (strcmp(iface->getFaceName(), IFACE_SERVICE) == 0) {
            iserv = static_cast<IService *>(iface);
            buf->write_string('{');
            buf->write_string("'Type':'");
            buf->write_string(iface->getFaceName());
            buf->write_string("','ModuleName':'");
            buf->write_string(iserv->getObjName());
            buf->write_string("'}");
        } else {
            RISCV_printf(NULL, LOG_ERROR, 
                        "Not implemented interface to dict. method");
        }
    } else if (attr->is_floating()) {
        char fstr[64];
        RISCV_sprintf(fstr, sizeof(fstr), "%.4f", attr->to_float());
        buf->write_string(fstr);
    }
    return buf->getBuffer();
}

const char *skip_special_symbols(const char *cfg) {
    const char *pcur = cfg;
    while (*pcur == ' ' || *pcur == '\r' || *pcur == '\n' || *pcur == '\t') {
        pcur++;
    }
    return pcur;
}

const char *string_to_attribute(const char *cfg, 
                          AttributeType *out) {
    const char *pcur = skip_special_symbols(cfg);
   
    if (pcur[0] == '\'' || pcur[0] == '"') {
        AutoBuffer buf;
        uint8_t t1 = pcur[0];
        int str_sz = 0;
        pcur++;
        while (*pcur != t1 && *pcur != '\0') {
            pcur++;
            str_sz++;
        }
        buf.write_bin(&cfg[1], str_sz);
        pcur++;
        out->make_string(buf.getBuffer());
    } else if (pcur[0] == '[') {
        pcur++;
        pcur = skip_special_symbols(pcur);
        AttributeType new_item;
        out->make_list(0);
        while (*pcur != ']' && *pcur != '\0') {
            pcur = string_to_attribute(pcur, &new_item);
            out->realloc_list(out->size() + 1);
            (*out)[out->size() - 1] = new_item;

            pcur = skip_special_symbols(pcur);
            if (*pcur == ',') {
                pcur++;
                pcur = skip_special_symbols(pcur);
            }
        }
        pcur++;
        pcur = skip_special_symbols(pcur);
    } else if (pcur[0] == '{') {
        AttributeType new_key;
        AttributeType new_value;
        out->make_dict();

        pcur++;
        pcur = skip_special_symbols(pcur);
        while (*pcur != '}' && *pcur != '\0') {
            pcur = string_to_attribute(pcur, &new_key);
            pcur = skip_special_symbols(pcur);
            if (*pcur == ':') {
                pcur++;
            }
            pcur = skip_special_symbols(pcur);
            pcur = string_to_attribute(pcur, &new_value);

            (*out)[new_key.to_string()] = new_value;

            pcur = skip_special_symbols(pcur);
            if (*pcur == ',') {
                pcur++;
                pcur = skip_special_symbols(pcur);
            }
        }
        pcur++;
        pcur = skip_special_symbols(pcur);

        if (out->has_key("Type")) {
            if (strcmp((*out)["Type"].to_string(), IFACE_SERVICE) == 0) {
                IService *iserv; 
                iserv = static_cast<IService *>(
                        RISCV_get_service((*out)["ModuleName"].to_string()));
                out->attr_free();
                *out = AttributeType(iserv);
            } else {
                RISCV_printf(NULL, LOG_ERROR, 
                        "Not implemented string to dict. attribute");
            }
        }
    } else if (pcur[0] == '(') {
        AutoBuffer buf;
        char byte_value;
        pcur++;
        pcur = skip_special_symbols(pcur);
        while (*pcur != ')' && *pcur != '\0') {
            byte_value = 0;
            for (int n = 0; n < 2; n++) {
                if (*pcur >= 'A' && *pcur <= 'F') {
                    byte_value = (byte_value << 4) | ((*pcur - 'A') + 10);
                } else {
                    byte_value = (byte_value << 4) | (*pcur - '0');
                }
                pcur++;
            }
            buf.write_bin(&byte_value, 1);

            pcur = skip_special_symbols(pcur);
            if (*pcur == ',') {
                pcur++;
                pcur = skip_special_symbols(pcur);
            }
        }
        out->make_data(buf.size(), buf.getBuffer());
        pcur++;
        pcur = skip_special_symbols(pcur);
    } else {
        pcur = skip_special_symbols(pcur);
        if (pcur[0] == 'N' && pcur[1] == 'o' && pcur[2] == 'n'
                && pcur[3] == 'e') {
            pcur += 4;
        } else if (pcur[0] == 'f' && pcur[1] == 'a' && pcur[2] == 'l'
                && pcur[3] == 's' && pcur[4] == 'e') {
            pcur += 5;
            out->make_boolean(false);
        } else if (pcur[0] == 't' && pcur[1] == 'r' && pcur[2] == 'u'
                && pcur[3] == 'e') {
            pcur += 4;
            out->make_boolean(true);
        } else {
            char digits[32] = {0};
            int digits_cnt = 0;
            if (pcur[0] == '0' && pcur[1] == 'x') {
                pcur += 2;
                digits[digits_cnt++] = '0';
                digits[digits_cnt++] = 'x';
            }
            while ((*pcur >= '0' && *pcur <= '9') 
                || (*pcur >= 'a' && *pcur <= 'f')
                || (*pcur >= 'A' && *pcur <= 'F')) {
                digits[digits_cnt++] = *pcur++;
            }
            int64_t t1 = strtoull(digits, NULL, 0);
            out->make_int64(t1);
        }
    }
    return pcur;
}

}  // namespace debugger
