/**
 * @file
 * @copyright  Copyright 2016 GNSS Sensor Ltd. All right reserved.
 * @author     Sergey Khabarov - sergeykhbr@gmail.com
 * @brief      Memory Editor area.
 */

#include "MemArea.h"
#include "moc_MemArea.h"

#include <memory>
#include <string.h>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QLabel>

namespace debugger {

MemArea::MemArea(IGui *gui, QWidget *parent) 
    : QPlainTextEdit(parent) {
    igui_ = gui;
    cmdRead_.make_string("read 0x80000000 20");
    data_.make_data(8);
    tmpBuf_.make_data(1024);
    dataText_.make_string("");

    clear();
    QFont font("Courier");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(8);
    font.setFixedPitch(true);
    setFont(font);
    QFontMetrics fm(font);
    setMinimumWidth(20 + fm.width(tr("[0011223344556677]: 11 22 33 44 55 66 77 88 ")));

    ensureCursorVisible();

    reqAddr_ = 0xfffff000;
    reqBytes_ = 20;

    connect(this, SIGNAL(signalUpdateData()), this, SLOT(slotUpdateData()));
}

void MemArea::slotAddressChanged(AttributeType *cmd) {
    reqAddr_ = (*cmd)[0u].to_uint64();
    reqBytes_ = static_cast<unsigned>((*cmd)[1].to_int());
}

void MemArea::slotUpdateByTimer() {
    char tstr[128];
    RISCV_sprintf(tstr, sizeof(tstr), "read 0x%08" RV_PRI64 "x %d",
                                        reqAddr_, reqBytes_);
    cmdRead_.make_string(tstr);
    igui_->registerCommand(static_cast<IGuiCmdHandler *>(this), 
                            &cmdRead_, true);
}

void MemArea::slotUpdateData() {
    moveCursor(QTextCursor::End);
    moveCursor(QTextCursor::Start, QTextCursor::KeepAnchor);
    QTextCursor cursor = textCursor();
    cursor.insertText(tr(dataText_.to_string()));
}

void MemArea::handleResponse(AttributeType *req, AttributeType *resp) {
    bool changed = false;
    if (resp->size() != reqBytes_) {
        changed = true;
    } else {
        for (unsigned i = 0; i < resp->size(); i++) {
            if ((*resp)(i) != data_(i)) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        return;
    }

    data_ = *resp;
    to_string(reqAddr_, reqBytes_, &dataText_);
    emit signalUpdateData();
}

void MemArea::to_string(uint64_t addr, uint64_t bytes, AttributeType *out) {
    const uint64_t MSK64 = 0x7ull;
    uint64_t addr_start, addr_end, inv_i;
    addr_start = addr & ~MSK64;
    addr_end = (addr + bytes + 7) & ~MSK64;

    if (tmpBuf_.size() < 4 * data_.size()) {
        tmpBuf_.make_data(4 * data_.size());
    }
    int strsz = 0;

    int  bufsz = tmpBuf_.size();
    char *strbuf = reinterpret_cast<char *>(tmpBuf_.data());
    for (uint64_t i = addr_start; i < addr_end; i++) {
        if ((i & MSK64) == 0) {
            // Output address:
            strsz += RISCV_sprintf(&strbuf[strsz], bufsz - strsz,
                                "[%016" RV_PRI64 "x]: ", i);
        }
        inv_i = (i & ~MSK64) | (MSK64 - (i & MSK64));
        if ((addr <= inv_i) && (inv_i < (addr + bytes))) {
            strsz += RISCV_sprintf(&strbuf[strsz], bufsz - strsz,
                                    " %02x", data_(inv_i - addr));
        } else {
            strsz += RISCV_sprintf(&strbuf[strsz], bufsz - strsz, " ..");
        }
        if ((i & MSK64) == MSK64) {
            strsz += RISCV_sprintf(&strbuf[strsz], bufsz - strsz, "\n");
        }
    }
    out->make_string(strbuf);
}


}  // namespace debugger
